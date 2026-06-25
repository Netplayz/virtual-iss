#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

echo "=== Building Virtual ISS ==="

# 1. Install Go dependencies
echo "--> Installing Go dependencies..."
for dir in subsystems/eps subsystems/comms subsystems/nas; do
    (cd "$dir" && go mod tidy && go build ./cmd/)
    echo "    Built $dir"
done

# 2. Build Rust workspace
echo "--> Building Rust workspace..."
cargo build --release 2>&1 | tail -5

# 3. Install Python dependencies
echo "--> Installing Python dependencies..."
for dir in subsystems/eclss subsystems/thermal subsystems/cdh subsystems/crew orchestrator; do
    if [ -f "$dir/requirements.txt" ]; then
        pip install -q -r "$dir/requirements.txt" 2>/dev/null
        echo "    Installed $dir"
    fi
done
pip install -q -r subsystems/mcp/requirements.txt 2>/dev/null
echo "    Installed mcp"

# 4. Dashboard
if command -v npm &>/dev/null; then
    echo "--> Installing dashboard dependencies..."
    (cd viz/dashboard && npm install 2>/dev/null)
fi

echo ""
echo "=== Build complete ==="
echo "Run: docker compose up        (containerized)"
echo "Or:  ./scripts/run.sh         (local)"
