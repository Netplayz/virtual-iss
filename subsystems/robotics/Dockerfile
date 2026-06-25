FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake g++ libeigen3-dev ca-certificates && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY subsystems/robotics/ ./
RUN cmake -B build && cmake --build build -j$(nproc)

FROM python:3.13-slim-bookworm
RUN pip install --no-cache-dir nats-py
COPY --from=builder /app/build/roboticsd /usr/local/bin/roboticsd
COPY subsystems/robotics/bridge.py /usr/local/bin/bridge.py
CMD ["python3", "/usr/local/bin/bridge.py"]
