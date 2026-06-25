FROM rust:1.86-slim-bookworm AS builder
WORKDIR /app
COPY Cargo.toml Cargo.lock ./
COPY dynamics dynamics/
COPY gnc gnc/
RUN cargo build --package dynamics --release

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=builder /app/target/release/dynamics /usr/local/bin/dynamics
CMD ["dynamics"]
