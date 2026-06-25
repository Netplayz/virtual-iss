# Virtual ISS — opencode Context

This is a 1:1 scale open-source simulation of the International Space Station's software stack. It spans 13 subsystems across 5 languages (Rust, Go, Python, C, C++), all connected via NATS JetStream.

## Key Files

- `AGENTS.md` — full build/test commands and conventions
- `docs/ARCHITECTURE.md` — system design
- `docs/SUBSYSTEMS.md` — per-module breakdown
- `docker-compose.yml` — all service definitions
- `bus/types.go` — canonical telemetry/command/event structs
- `bus/nats.conf` — NATS server with JetStream, accounts, permissions

## What's Done

All 13 subsystem implementations are complete. The robotics C++ module was just committed (`41fa75f`) — it builds with Eigen3 and passes 2 unit tests. MDM has a Makefile for local build but no Dockerfile yet.

## What's Next

- Add Dockerfile for MDM (+ compose entry)
- Add Dockerfile for Robotics (+ compose entry)
- Add NATS client integration to Robotics (currently standalone loop)
- Add fault handlers to subsystems (EPS per-panel, ECLSS per-system, GNC sensor)
- Add missing TOML configs for GNC, thermal, comms, C&DH, crew, robotics, MDM
- Add integration tests (end-to-end telemetry flow)
- Add checkpoint/restore API

## Critical Rules

1. **Never add comments to source code** unless the code is genuinely unclear
2. **Never create documentation files** (`*.md`) unless explicitly asked — AGENTS.md and CLAUDE.md are the exceptions
3. **Never use emojis** unless the user explicitly requests them
4. **Never commit** unless the user says "commit" or "merge"
5. **Never remove `*.o` from `.gitignore`** — build artifacts stay ignored
6. **Keep responses under 4 lines** unless detail is requested
7. **Prefer editing existing files** — never write new files unless necessary
8. **Verify with lint/typecheck/build** after every code change
9. **All telemetry is JSON** on NATS subjects `telemetry.<subsystem>.<type>`
10. **All commands** arrive on `command.uplink` with opcode dispatch

## Build Verification

After any Rust change: `cargo build && cargo test`
After any Go change: `go build ./... && go vet ./...`
After any C change: `make -C subsystems/mdm`
After any C++ change: `cmake -B subsystems/robotics/build && cmake --build subsystems/robotics/build && ctest --test-dir subsystems/robotics/build`
After any Python change: `python -m py_compile <file>` (syntax check)
