FROM golang:1.24-alpine AS builder
WORKDIR /app
COPY subsystems/comms/go.mod subsystems/comms/go.sum ./comms/
COPY subsystems/comms/ ./comms/
COPY bus/ ./bus/
WORKDIR /app/comms
RUN go build -o /comms ./cmd/

FROM alpine:3.21
COPY --from=builder /comms /usr/local/bin/comms
CMD ["comms"]
