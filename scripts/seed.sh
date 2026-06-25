#!/usr/bin/env bash
# Load a scenario into the orchestrator
set -euo pipefail

SCENARIO="${1:-nominal_ops}"
ORCH_HOST="${ORCHESTRATOR_HOST:-localhost:8300}"

echo "Loading scenario '$SCENARIO' into orchestrator at $ORCH_HOST..."

curl -s -X POST "http://$ORCH_HOST/api/v1/scenario/load" \
  -H "Content-Type: application/json" \
  -d "{\"scenario_id\": \"$SCENARIO\"}" | jq .

echo "Scenario loaded. Starting simulation..."
curl -s -X POST "http://$ORCH_HOST/api/v1/control/resume" | jq .
