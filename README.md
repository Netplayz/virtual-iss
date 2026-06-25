# Virtual ISS

Open-source 1:1 scale virtual simulation of the International Space Station.

## Architecture

```
Orchestrator ──► NATS Bus ◄── Subsystems (Rust/Go/Python)
                      │
                      ├── NAS (Storage Service)
                      ├── MCP (AI Agent Interface)
                      └── Viz (Godot 4 + OpenMCT)
```

- **Dynamics** (Rust) — SGP4 orbit, quaternion attitude, CMG, environment torques
- **GNC** (Rust) — Star tracker/gyro/GPS sensors, EKF, PID control, CMG steering
- **EPS** (Go) — Solar arrays, Li-Ion batteries, power distribution, load shedding
- **ECLSS** (Python) — O₂ generation, CO₂ scrubbing, water recovery, cabin control
- **Thermal** (Python) — Active ammonia loops, radiators, heaters
- **Comms** (Go) — TDRSS, S-band/Ku-band links, latency simulation
- **C&DH** (Python) — Command verification, telemetry packing, limit monitoring
- **Crew** (Python) — Activity timeline, consumables, task execution
- **NAS** (Go) — Telemetry recording/replay, object storage, HTTP API
- **MCP** (Python) — Model Context Protocol server for AI agent integration
- **Orchestrator** (Python) — Time sync, scenario control, fault injection

## Quick Start

```bash
# All services via Docker
docker compose up

# Or build locally
./scripts/build.sh
./scripts/run.sh
```

## Scenarios

| Scenario | Description |
|----------|-------------|
| `nominal_ops` | Standard 90-min orbit, all subsystems nominal |
| `load_shed_test` | Solar array failure triggering automatic load shedding |

## Extending

Add a new subsystem:
1. Create module in `subsystems/`
2. Subscribe to `orchestrator.tick` for timing
3. Publish state to `telemetry.<name>.state` as JSON
4. Handle commands from `command.uplink`
5. Add to `docker-compose.yml`

## License

MIT
