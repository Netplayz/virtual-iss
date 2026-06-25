package nas

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/nats-io/nats.go"
	"github.com/sirupsen/logrus"
)

type RecordedFrame struct {
	Subject   string    `json:"subject"`
	Data      []byte    `json:"data"`
	Timestamp time.Time `json:"timestamp"`
}

type StreamWriter struct {
	file   *os.File
	encoder *json.Encoder
	mu     sync.Mutex
	count  int64
}

type Recorder struct {
	nc       *nats.Conn
	dataDir  string
	streams  map[string]*StreamWriter
	subs     []*nats.Subscription
	mu       sync.RWMutex
	log      *logrus.Entry
}

func NewRecorder(nc *nats.Conn, dataDir string) *Recorder {
	streamsDir := filepath.Join(dataDir, "streams")
	if err := os.MkdirAll(streamsDir, 0755); err != nil {
		logrus.WithError(err).Fatal("failed to create streams directory")
	}
	return &Recorder{
		nc:      nc,
		dataDir: dataDir,
		streams: make(map[string]*StreamWriter),
		log:     logrus.WithField("component", "recorder"),
	}
}

func (r *Recorder) RecordTelemetry(streamName string, subjects []string, duration time.Duration) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if _, exists := r.streams[streamName]; exists {
		return fmt.Errorf("stream %s already recording", streamName)
	}

	streamDir := filepath.Join(r.dataDir, "streams", streamName)
	if err := os.MkdirAll(streamDir, 0755); err != nil {
		return fmt.Errorf("failed to create stream directory: %w", err)
	}

	filename := fmt.Sprintf("%s_%s.jsonl", streamName, time.Now().UTC().Format("20060102T150405Z"))
	filePath := filepath.Join(streamDir, filename)
	f, err := os.Create(filePath)
	if err != nil {
		return fmt.Errorf("failed to create recording file: %w", err)
	}

	sw := &StreamWriter{
		file:    f,
		encoder: json.NewEncoder(f),
	}

	var subs []*nats.Subscription
	for _, subject := range subjects {
		sub, err := r.nc.Subscribe(subject, func(msg *nats.Msg) {
			r.WriteFrame(streamName, msg.Subject, msg.Data, time.Now().UTC())
		})
		if err != nil {
			for _, s := range subs {
				s.Unsubscribe()
			}
			f.Close()
			os.Remove(filePath)
			return fmt.Errorf("failed to subscribe to %s: %w", subject, err)
		}
		subs = append(subs, sub)
	}

	r.streams[streamName] = sw
	r.subs = append(r.subs, subs...)

	r.log.WithFields(logrus.Fields{
		"stream":   streamName,
		"subjects": subjects,
		"duration": duration,
		"file":     filename,
	}).Info("started recording")

	if duration > 0 {
		time.AfterFunc(duration, func() {
			if err := r.CloseStream(streamName); err != nil {
				r.log.WithError(err).WithField("stream", streamName).Error("failed to close stream after duration")
			}
		})
	}

	return nil
}

func (r *Recorder) WriteFrame(streamName string, subject string, data []byte, timestamp time.Time) {
	r.mu.RLock()
	sw, exists := r.streams[streamName]
	r.mu.RUnlock()
	if !exists {
		return
	}

	frame := RecordedFrame{
		Subject:   subject,
		Data:      data,
		Timestamp: timestamp,
	}

	sw.mu.Lock()
	defer sw.mu.Unlock()
	if err := sw.encoder.Encode(frame); err != nil {
		r.log.WithError(err).WithField("stream", streamName).Error("failed to write frame")
		return
	}
	sw.count++
}

func (r *Recorder) CloseStream(streamName string) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	sw, exists := r.streams[streamName]
	if !exists {
		return fmt.Errorf("stream %s not found", streamName)
	}

	var activeSubs []*nats.Subscription
	for _, sub := range r.subs {
		if sub.IsValid() {
			activeSubs = append(activeSubs, sub)
		}
	}
	r.subs = activeSubs

	if err := sw.file.Close(); err != nil {
		return fmt.Errorf("failed to close stream file: %w", err)
	}

	delete(r.streams, streamName)

	r.log.WithFields(logrus.Fields{
		"stream": streamName,
		"frames": sw.count,
	}).Info("closed recording stream")

	return nil
}

func (r *Recorder) ListStreams() ([]string, error) {
	streamsDir := filepath.Join(r.dataDir, "streams")
	entries, err := os.ReadDir(streamsDir)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}

	var streams []string
	for _, e := range entries {
		if e.IsDir() {
			streams = append(streams, e.Name())
		}
	}
	return streams, nil
}

func (r *Recorder) GetStreamInfo(streamName string) (int64, time.Time, time.Time, error) {
	streamDir := filepath.Join(r.dataDir, "streams", streamName)
	entries, err := os.ReadDir(streamDir)
	if err != nil {
		return 0, time.Time{}, time.Time{}, err
	}

	var totalFrames int64
	var firstFrame, lastFrame RecordedFrame
	firstSet := false

	for _, e := range entries {
		if filepath.Ext(e.Name()) != ".jsonl" {
			continue
		}
		filePath := filepath.Join(streamDir, e.Name())
		data, err := os.ReadFile(filePath)
		if err != nil {
			continue
		}
		lines := splitLines(data)
		totalFrames += int64(len(lines))
		for _, line := range lines {
			if line == "" {
				continue
			}
			var frame RecordedFrame
			if err := json.Unmarshal([]byte(line), &frame); err != nil {
				continue
			}
			if !firstSet || frame.Timestamp.Before(firstFrame.Timestamp) {
				firstFrame = frame
				firstSet = true
			}
			if frame.Timestamp.After(lastFrame.Timestamp) {
				lastFrame = frame
			}
		}
	}

	if !firstSet {
		return 0, time.Time{}, time.Time{}, fmt.Errorf("no frames found in stream %s", streamName)
	}

	return totalFrames, firstFrame.Timestamp, lastFrame.Timestamp, nil
}

func splitLines(data []byte) []string {
	var lines []string
	start := 0
	for i, b := range data {
		if b == '\n' {
			lines = append(lines, string(data[start:i]))
			start = i + 1
		}
	}
	if start < len(data) {
		lines = append(lines, string(data[start:]))
	}
	return lines
}
