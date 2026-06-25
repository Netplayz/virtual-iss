package nas

import (
	"context"
	"errors"
	"fmt"

	"github.com/nats-io/nats.go"
	"github.com/nats-io/nats.go/jetstream"
	"github.com/sirupsen/logrus"
)

type ObjectStore struct {
	nc      *nats.Conn
	js      jetstream.JetStream
	stores  map[string]jetstream.ObjectStore
	log     *logrus.Entry
}

func NewObjectStore(nc *nats.Conn) *ObjectStore {
	js, err := jetstream.New(nc)
	if err != nil {
		logrus.WithError(err).Fatal("failed to create JetStream context")
	}
	return &ObjectStore{
		nc:     nc,
		js:     js,
		stores: make(map[string]jetstream.ObjectStore),
		log:    logrus.WithField("component", "object_store"),
	}
}

func (s *ObjectStore) getOrCreateBucket(bucket string) (jetstream.ObjectStore, error) {
	if store, exists := s.stores[bucket]; exists {
		return store, nil
	}

	ctx := context.Background()

	store, err := s.js.CreateOrUpdateObjectStore(ctx, jetstream.ObjectStoreConfig{
		Bucket:      bucket,
		Description: "Virtual ISS object store bucket: " + bucket,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to create/open object store bucket %s: %w", bucket, err)
	}

	s.stores[bucket] = store
	return store, nil
}

func (s *ObjectStore) Put(bucket string, key string, data []byte) error {
	store, err := s.getOrCreateBucket(bucket)
	if err != nil {
		return err
	}

	ctx := context.Background()
	_, err = store.PutBytes(ctx, key, data)
	if err != nil {
		return fmt.Errorf("failed to put object %s/%s: %w", bucket, key, err)
	}

	s.log.WithFields(logrus.Fields{
		"bucket": bucket,
		"key":    key,
		"size":   len(data),
	}).Info("stored object")

	return nil
}

func (s *ObjectStore) Get(bucket string, key string) ([]byte, error) {
	store, err := s.getOrCreateBucket(bucket)
	if err != nil {
		return nil, err
	}

	ctx := context.Background()
	data, err := store.GetBytes(ctx, key)
	if err != nil {
		if errors.Is(err, jetstream.ErrObjectNotFound) {
			return nil, fmt.Errorf("object %s/%s not found", bucket, key)
		}
		return nil, fmt.Errorf("failed to get object %s/%s: %w", bucket, key, err)
	}

	return data, nil
}

func (s *ObjectStore) Delete(bucket string, key string) error {
	store, err := s.getOrCreateBucket(bucket)
	if err != nil {
		return err
	}

	ctx := context.Background()
	if err := store.Delete(ctx, key); err != nil {
		return fmt.Errorf("failed to delete object %s/%s: %w", bucket, key, err)
	}

	s.log.WithFields(logrus.Fields{
		"bucket": bucket,
		"key":    key,
	}).Info("deleted object")

	return nil
}

func (s *ObjectStore) List(bucket string) ([]string, error) {
	store, err := s.getOrCreateBucket(bucket)
	if err != nil {
		return nil, err
	}

	ctx := context.Background()
	objects, err := store.List(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to list objects in bucket %s: %w", bucket, err)
	}

	var keys []string
	for _, obj := range objects {
		keys = append(keys, obj.Name)
	}

	return keys, nil
}
