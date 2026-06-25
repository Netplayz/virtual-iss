# Virtual ISS — Agent Instructions

## Repository

https://github.com/Netplayz/virtual-iss

## Build & Test Commands

```bash
# Rust (dynamics, GNC)
cargo build                       # entire workspace
cargo test                        # all Rust tests
cargo test -p dynamics            # single crate

# Go (EPS, Comms, NAS)
cd subsystems/<name> && go build ./... && go test ./...

# Python (ECLSS, Thermal, C&DH, Crew, MCP, Orchestrator)
python -m pytest <subsystem>/    # if tests exist

# C (MDM)
cd subsystems/mdm && make

# C++ (Robotics)
cd subsystems/robotics && cmake -B build && cmake --build build && ctest --test-dir build

# Docker (all services)
docker compose build <service>   # single service
docker compose up                # full stack

# Scripts
./scripts/build.sh               # build all
./scripts/run.sh                 # run locally
./scripts/seed.sh <scenario>     # load scenario
```

## Code Conventions

- **No comments** in source code unless the code's purpose is genuinely unclear
- **Lines**: ~100 char max, no strict rule
- **Naming**: snake_case for Rust/Python/C, camelCase for Go, PascalCase for C++ types
- **Error handling**: Rust — `anyhow` for binaries, `thiserror` for libs; Go — return errors, no panics; Python — typed exceptions, log and raise
- **Telemetry**: all subsystems publish JSON to `telemetry.<name>.state` on NATS
- **Commands**: subscribe to `command.uplink` and dispatch by opcode
- **Tick**: subscribe to `orchestrator.tick` for timing; never use wall-clock sleep

## Architecture

All 13 subsystems communicate exclusively via NATS JetStream at `nats://localhost:4222`. Each publishes telemetry on tick and listens for commands. The Orchestrator drives simulation time. MCP provides an HTTP SSE bridge for external tools (Godot, OpenMCT). NAS records and replays telemetry streams.

## Subsystem Language Map

| Subsystem | Lang | Path |
|-----------|------|------|
| Dynamics | Rust | `dynamics/` |
| GNC | Rust | `gnc/` |
| EPS | Go | `subsystems/eps/` |
| Comms | Go | `subsystems/comms/` |
| ECLSS | Python | `subsystems/eclss/` |
| Thermal | Python | `subsystems/thermal/` |
| C&DH | Python | `subsystems/cdh/` |
| Crew | Python | `subsystems/crew/` |
| MDM | C | `subsystems/mdm/` |
| Robotics | C++ | `subsystems/robotics/` |
| NAS | Go | `subsystems/nas/` |
| MCP | Python | `subsystems/mcp/` |
| Orchestrator | Python | `orchestrator/` |
| Viz (Godot) | GDScript | `viz/godot/` |
| Dashboard | JS/React | `viz/openmct/` |

## Critical Notes

- NATS must be running before any subsystem (port 4222, JetStream enabled)
- The build directory for robotics is `subsystems/robotics/build/` (gitignored)
- Scenario files in `configs/scenarios/` are JSON
- Bus type definitions live in `bus/types.go` (canonical) and `bus/subjects.md`
- All Go modules use `nats.io/nats.go` v2; Rust uses `async-nats`
- Python services use `asyncio-nats-client` and `fastapi` for HTTP
- Do NOT commit `.o`, `.a`, `.so`, build artifacts — they are in `.gitignore`
- Do NOT remove `*.o` from `.gitignore`
