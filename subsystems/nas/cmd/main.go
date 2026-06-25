package main

import (
	"context"
	"net/http"
	"os"
	"os/signal"
	"syscall"

	"github.com/nats-io/nats.go"
	"github.com/sirupsen/logrus"
	"github.com/virtual-iss/nas"
)

func main() {
	logrus.SetFormatter(&logrus.TextFormatter{
		FullTimestamp: true,
	})
	log := logrus.WithField("subsystem", "nas")

	natsURL := os.Getenv("NATS_URL")
	if natsURL == "" {
		natsURL = nats.DefaultURL
	}

	nc, err := nats.Connect(natsURL)
	if err != nil {
		log.WithError(err).Fatal("failed to connect to NATS")
	}
	defer nc.Close()

	log.WithField("url", natsURL).Info("connected to NATS")

	dataDir := os.Getenv("NAS_DATA_DIR")
	if dataDir == "" {
		dataDir = "/data"
	}

	svc := nas.NewNASService(nc, dataDir)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		sig := <-sigCh
		log.WithField("signal", sig).Info("shutting down")
		svc.Shutdown(context.Background())
		cancel()
	}()

	go func() {
		if err := svc.ServeHTTP(8330); err != nil && err != http.ErrServerClosed {
			log.WithError(err).Fatal("HTTP server error")
		}
	}()

	if err := svc.Start(ctx); err != nil {
		log.WithError(err).Fatal("service error")
	}
}
