FROM gcc:14-bookworm AS builder
WORKDIR /app
COPY subsystems/mdm/ ./
RUN make clean && make CFLAGS="-O2 -Wall -Wextra -pedantic -std=c89" LDFLAGS="-lm"

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y python3 python3-pip && \
    pip3 install nats-py --quiet && \
    rm -rf /var/lib/apt/lists/*
COPY --from=builder /app/mdm /usr/local/bin/mdm
COPY subsystems/mdm/bridge.py /usr/local/bin/bridge.py
EXPOSE 0
CMD ["python3", "/usr/local/bin/bridge.py"]
