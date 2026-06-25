#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

NATS_URL="${NATS_URL:-nats://localhost:4222}"

echo "=== Starting Virtual ISS (local mode) ==="
echo "NATS: $NATS_URL"

# Start NATS if not running
if ! nc -z localhost 4222 2>/dev/null; then
    echo "--> Starting NATS server..."
    docker run -d --name virtual-iss-nats -p 4222:4222 -p 8222:8222 nats:2.10-alpine -js
    sleep 2
fi

# Start all services in background
echo "--> Starting services..."

cargo run --release --package dynamics &
PID_DYN=$!

cargo run --release --package gnc &
PID_GNC=$!

(cd subsystems/eps && go run ./cmd/) &
PID_EPS=$!

(cd subsystems/comms && go run ./cmd/) &
PID_COMMS=$!

(cd subsystems/nas && go run ./cmd/) &
PID_NAS=$!

python subsystems/eclss/main.py &
PID_ECLSS=$!

python subsystems/thermal/main.py &
PID_THERMAL=$!

python subsystems/cdh/main.py &
PID_CDH=$!

python subsystems/crew/main.py &
PID_CREW=$!

python subsystems/mcp/main.py &
PID_MCP=$!

python orchestrator/main.py &
PID_ORCH=$!

# Start dashboard
(cd viz/dashboard && node server.js) &
PID_DASH=$!

echo ""
echo "=== All services started ==="
echo "Dashboard: http://localhost:8320"
echo "MCP API:   http://localhost:8331"
echo "NAS API:   http://localhost:8330"
echo "NATS:      http://localhost:8222"
echo ""
echo "Press Ctrl+C to stop all services"

cleanup() {
    echo "--> Stopping all services..."
    kill $PID_DYN $PID_GNC $PID_EPS $PID_COMMS $PID_ECLSS $PID_THERMAL $PID_CDH $PID_CREW $PID_ORCH $PID_NAS $PID_MCP $PID_DASH 2>/dev/null
    wait
    echo "Done."
}
trap cleanup EXIT INT TERM

# Wait for any process to exit
wait
