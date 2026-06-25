FROM golang:1.24-alpine AS builder

WORKDIR /src
COPY subsystems/nas/go.mod subsystems/nas/go.sum ./
RUN go mod download

COPY subsystems/nas/ ./
RUN CGO_ENABLED=0 go build -o /nas ./cmd/

FROM alpine:3.20
RUN apk add --no-cache ca-certificates
COPY --from=builder /nas /usr/local/bin/nas
ENTRYPOINT ["nas"]
