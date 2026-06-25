package main

import (
	"context"
	"os"
	"os/signal"
	"syscall"

	"github.com/nats-io/nats.go"
	"github.com/sirupsen/logrus"
	"github.com/virtual-iss/eps"
)

func main() {
	logrus.SetFormatter(&logrus.TextFormatter{
		FullTimestamp: true,
	})
	log := logrus.WithField("subsystem", "eps")

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

	sim := eps.NewEPSSimulator(nc)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		sig := <-sigCh
		log.WithField("signal", sig).Info("shutting down")
		cancel()
	}()

	if err := sim.Run(ctx); err != nil {
		log.WithError(err).Fatal("simulator error")
	}
}
