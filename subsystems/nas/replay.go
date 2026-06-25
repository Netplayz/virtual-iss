package nas

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"time"

	"github.com/nats-io/nats.go"
	"github.com/sirupsen/logrus"
)

type Replayer struct {
	nc      *nats.Conn
	dataDir string
	log     *logrus.Entry
}

func NewReplayer(nc *nats.Conn, dataDir string) *Replayer {
	return &Replayer{
		nc:      nc,
		dataDir: dataDir,
		log:     logrus.WithField("component", "replayer"),
	}
}

func (r *Replayer) ReplayStream(streamName string, startTime time.Time, endTime time.Time, rate float64) error {
	streamDir := filepath.Join(r.dataDir, "streams", streamName)
	entries, err := os.ReadDir(streamDir)
	if err != nil {
		return fmt.Errorf("failed to read stream directory: %w", err)
	}

	var allFrames []RecordedFrame
	for _, e := range entries {
		if filepath.Ext(e.Name()) != ".jsonl" {
			continue
		}
		filePath := filepath.Join(streamDir, e.Name())
		data, err := os.ReadFile(filePath)
		if err != nil {
			r.log.WithError(err).WithField("file", e.Name()).Warn("skipping unreadable file")
			continue
		}
		lines := splitLines(data)
		for _, line := range lines {
			if line == "" {
				continue
			}
			var frame RecordedFrame
			if err := json.Unmarshal([]byte(line), &frame); err != nil {
				continue
			}
			if !startTime.IsZero() && frame.Timestamp.Before(startTime) {
				continue
			}
			if !endTime.IsZero() && frame.Timestamp.After(endTime) {
				continue
			}
			allFrames = append(allFrames, frame)
		}
	}

	if len(allFrames) == 0 {
		return fmt.Errorf("no frames to replay in stream %s", streamName)
	}

	sort.Slice(allFrames, func(i, j int) bool {
		return allFrames[i].Timestamp.Before(allFrames[j].Timestamp)
	})

	r.log.WithFields(logrus.Fields{
		"stream": streamName,
		"frames": len(allFrames),
		"rate":   rate,
	}).Info("starting replay")

	if rate <= 0 {
		rate = 1.0
	}

	baseTime := allFrames[0].Timestamp
	now := time.Now()

	for i, frame := range allFrames {
		if rate > 0 {
			elapsed := frame.Timestamp.Sub(baseTime)
			sleepDuration := time.Duration(float64(elapsed) / rate)
			targetTime := now.Add(sleepDuration)

			if i > 0 {
				wait := time.Until(targetTime)
				if wait > 0 {
					time.Sleep(wait)
				}
			}
		}

		if err := r.nc.Publish(frame.Subject, frame.Data); err != nil {
			r.log.WithError(err).WithField("subject", frame.Subject).Error("failed to publish frame during replay")
		}
	}

	r.log.WithField("stream", streamName).Info("replay complete")
	return nil
}

func (r *Replayer) ListStreams() ([]string, error) {
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

func (r *Replayer) GetStreamInfo(streamName string) (frameCount int64, startTime time.Time, endTime time.Time, err error) {
	return NewRecorder(r.nc, r.dataDir).GetStreamInfo(streamName)
}

func (r *Replayer) GetStreamFiles(streamName string) ([]string, error) {
	streamDir := filepath.Join(r.dataDir, "streams", streamName)
	entries, err := os.ReadDir(streamDir)
	if err != nil {
		return nil, err
	}

	var files []string
	for _, e := range entries {
		if !e.IsDir() {
			files = append(files, filepath.Join(streamDir, e.Name()))
		}
	}

	sort.Slice(files, func(i, j int) bool {
		return files[i] < files[j]
	})

	return files, nil
}

func (r *Replayer) ReadAllFrames(streamName string) ([]RecordedFrame, error) {
	streamDir := filepath.Join(r.dataDir, "streams", streamName)
	entries, err := os.ReadDir(streamDir)
	if err != nil {
		return nil, err
	}

	var frames []RecordedFrame
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
		for _, line := range lines {
			if line == "" {
				continue
			}
			var frame RecordedFrame
			if err := json.Unmarshal([]byte(line), &frame); err != nil {
				continue
			}
			frames = append(frames, frame)
		}
	}

	sort.Slice(frames, func(i, j int) bool {
		return frames[i].Timestamp.Before(frames[j].Timestamp)
	})

	return frames, nil
}

func (r *Replayer) StreamSize(streamName string) (int64, error) {
	streamDir := filepath.Join(r.dataDir, "streams", streamName)
	var total int64
	err := filepath.Walk(streamDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() {
			total += info.Size()
		}
		return nil
	})
	return total, err
}


