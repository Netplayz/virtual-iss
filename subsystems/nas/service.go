package nas

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/nats-io/nats.go"
	"github.com/sirupsen/logrus"
)

type NASService struct {
	nc       *nats.Conn
	recorder *Recorder
	replayer *Replayer
	store    *ObjectStore
	dataDir  string
	log      *logrus.Entry
	httpSrv  *http.Server
}

func NewNASService(nc *nats.Conn, dataDir string) *NASService {
	if dataDir == "" {
		dataDir = os.Getenv("NAS_DATA_DIR")
	}
	if dataDir == "" {
		dataDir = "/data"
	}
	os.MkdirAll(dataDir, 0755)

	return &NASService{
		nc:       nc,
		recorder: NewRecorder(nc, dataDir),
		replayer: NewReplayer(nc, dataDir),
		store:    NewObjectStore(nc),
		dataDir:  dataDir,
		log:      logrus.WithField("component", "nas"),
	}
}

func (s *NASService) Start(ctx context.Context) error {
	s.log.Info("starting NAS service")

	subs := []struct {
		subject string
		handler nats.MsgHandler
	}{
		{"nas.record", s.handleRecord},
		{"nas.replay", s.handleReplay},
		{"nas.store", s.handleStore},
		{"nas.retrieve", s.handleRetrieve},
	}

	for _, sub := range subs {
		_, err := s.nc.Subscribe(sub.subject, sub.handler)
		if err != nil {
			return fmt.Errorf("failed to subscribe to %s: %w", sub.subject, err)
		}
		s.log.WithField("subject", sub.subject).Info("subscribed")
	}

	// drain on ctx cancel
	go func() {
		<-ctx.Done()
		if err := s.nc.Drain(); err != nil {
			s.log.WithError(err).Warn("nats drain error")
		}
	}()

	s.log.Info("NAS service started")
	<-ctx.Done()
	return nil
}

func (s *NASService) ServeHTTP(port int) error {
	mux := http.NewServeMux()

	mux.HandleFunc("/api/v1/streams", s.handleListStreams)
	mux.HandleFunc("/api/v1/streams/", s.handleStreamDownload)
	mux.HandleFunc("/api/v1/store", s.handleStoreHTTP)
	mux.HandleFunc("/api/v1/store/", s.handleRetrieveHTTP)
	mux.HandleFunc("/api/v1/status", s.handleStatus)

	s.httpSrv = &http.Server{
		Addr:    fmt.Sprintf(":%d", port),
		Handler: mux,
	}

	s.log.WithField("port", port).Info("starting HTTP server")
	return s.httpSrv.ListenAndServe()
}

func (s *NASService) Shutdown(ctx context.Context) error {
	if s.httpSrv != nil {
		return s.httpSrv.Shutdown(ctx)
	}
	return nil
}

func (s *NASService) handleRecord(msg *nats.Msg) {
	var req struct {
		Stream   string   `json:"stream"`
		Subjects []string `json:"subjects"`
		Duration string   `json:"duration"`
	}
	if err := json.Unmarshal(msg.Data, &req); err != nil {
		s.respond(msg, map[string]any{"error": err.Error()})
		return
	}

	var dur time.Duration
	if req.Duration != "" {
		dur, _ = time.ParseDuration(req.Duration)
	}

	if err := s.recorder.RecordTelemetry(req.Stream, req.Subjects, dur); err != nil {
		s.respond(msg, map[string]any{"error": err.Error()})
		return
	}

	s.respond(msg, map[string]any{"status": "recording", "stream": req.Stream})
}

func (s *NASService) handleReplay(msg *nats.Msg) {
	var req struct {
		Stream string  `json:"stream"`
		Start  string  `json:"start"`
		End    string  `json:"end"`
		Rate   float64 `json:"rate"`
	}
	if err := json.Unmarshal(msg.Data, &req); err != nil {
		s.respond(msg, map[string]any{"error": err.Error()})
		return
	}

	var startTime, endTime time.Time
	if req.Start != "" {
		startTime, _ = time.Parse(time.RFC3339, req.Start)
	}
	if req.End != "" {
		endTime, _ = time.Parse(time.RFC3339, req.End)
	}

	go func() {
		if err := s.replayer.ReplayStream(req.Stream, startTime, endTime, req.Rate); err != nil {
			s.log.WithError(err).WithField("stream", req.Stream).Error("replay failed")
		}
	}()

	s.respond(msg, map[string]any{"status": "replaying", "stream": req.Stream})
}

func (s *NASService) handleStore(msg *nats.Msg) {
	var req struct {
		Bucket  string `json:"bucket"`
		Key     string `json:"key"`
		Payload []byte `json:"payload"`
	}
	if err := json.Unmarshal(msg.Data, &req); err != nil {
		s.respond(msg, map[string]any{"error": err.Error()})
		return
	}

	if err := s.store.Put(req.Bucket, req.Key, req.Payload); err != nil {
		s.respond(msg, map[string]any{"error": err.Error()})
		return
	}

	s.respond(msg, map[string]any{"status": "stored", "bucket": req.Bucket, "key": req.Key})
}

func (s *NASService) handleRetrieve(msg *nats.Msg) {
	var req struct {
		Bucket string `json:"bucket"`
		Key    string `json:"key"`
	}
	if err := json.Unmarshal(msg.Data, &req); err != nil {
		s.respond(msg, map[string]any{"error": err.Error()})
		return
	}

	data, err := s.store.Get(req.Bucket, req.Key)
	if err != nil {
		s.respond(msg, map[string]any{"error": err.Error()})
		return
	}

	s.respond(msg, map[string]any{
		"bucket": req.Bucket,
		"key":    req.Key,
		"data":   data,
	})
}

func (s *NASService) handleListStreams(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	streams, err := s.recorder.ListStreams()
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	var infos []map[string]any
	for _, name := range streams {
		frames, start, end, err := s.recorder.GetStreamInfo(name)
		if err != nil {
			continue
		}
		infos = append(infos, map[string]any{
			"name":       name,
			"frames":     frames,
			"start_time": start,
			"end_time":   end,
		})
	}

	writeJSON(w, http.StatusOK, map[string]any{"streams": infos})
}

func (s *NASService) handleStreamDownload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	parts := strings.Split(strings.TrimPrefix(r.URL.Path, "/api/v1/streams/"), "/")
	if len(parts) < 1 || parts[0] == "" {
		http.Error(w, "stream id required", http.StatusBadRequest)
		return
	}
	streamName := parts[0]

	files, err := s.replayer.GetStreamFiles(streamName)
	if err != nil {
		http.Error(w, "stream not found", http.StatusNotFound)
		return
	}

	if len(files) == 0 {
		http.Error(w, "no files in stream", http.StatusNotFound)
		return
	}

	// if requesting specific file, serve it
	if len(parts) >= 2 && parts[1] != "" {
		filePath := filepath.Join(s.dataDir, "streams", streamName, parts[1])
		http.ServeFile(w, r, filePath)
		return
	}

	// otherwise download all as concatenated JSONL
	w.Header().Set("Content-Type", "application/x-ndjson")
	w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=\"%s.jsonl\"", streamName))

	for _, f := range files {
		data, err := os.ReadFile(f)
		if err != nil {
			continue
		}
		w.Write(data)
	}
}

func (s *NASService) handleStoreHTTP(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodPost:
		var req struct {
			Bucket  string `json:"bucket"`
			Key     string `json:"key"`
			Payload []byte `json:"payload"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		if err := s.store.Put(req.Bucket, req.Key, req.Payload); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"status": "stored", "bucket": req.Bucket, "key": req.Key})
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func (s *NASService) handleRetrieveHTTP(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	parts := strings.Split(strings.TrimPrefix(r.URL.Path, "/api/v1/store/"), "/")
	if len(parts) < 2 {
		http.Error(w, "bucket/key required", http.StatusBadRequest)
		return
	}

	bucket := parts[0]
	key := strings.Join(parts[1:], "/")

	data, err := s.store.Get(bucket, key)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}

	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Length", strconv.Itoa(len(data)))
	w.Write(data)
}

func (s *NASService) handleStatus(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{
		"status":  "ok",
		"service": "nas",
		"time":    time.Now().UTC(),
	})
}

func (s *NASService) respond(msg *nats.Msg, data any) {
	resp, err := json.Marshal(data)
	if err != nil {
		s.log.WithError(err).Error("failed to marshal response")
		return
	}
	msg.Respond(resp)
}

func writeJSON(w http.ResponseWriter, status int, data any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}

