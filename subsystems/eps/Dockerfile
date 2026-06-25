FROM golang:1.24-alpine AS builder
WORKDIR /app
COPY subsystems/eps/go.mod subsystems/eps/go.sum ./eps/
COPY subsystems/eps/ ./eps/
COPY bus/ ./bus/
WORKDIR /app/eps
RUN go build -o /eps ./cmd/

FROM alpine:3.21
COPY --from=builder /eps /usr/local/bin/eps
CMD ["eps"]
