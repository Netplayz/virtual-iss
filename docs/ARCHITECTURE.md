# Virtual ISS Architecture

## System Overview

Virtual ISS is a distributed microservice simulation connected via a NATS message bus. Each subsystem runs as an independent service, communicating through typed JSON messages on well-defined NATS subjects.

## Message Bus

- NATS JetStream-enabled for persistence and replay
- Subjects follow: `<domain>.<subsystem>.<type>[.<qualifier>]`
- All telemetry published to `telemetry.*` subjects
- Commands use request/reply on `command.uplink`
- Orchestrator drives time via `orchestrator.tick`

## Data Flow

```
[Orchestrator]   tick ──► [All Subsystems] ──► telemetry.* ──► [NAS/C&DH/Vis]
                    │                               │
                    └── scenario control ────────────┘
```

## Subsystem Communication Pattern

Each subsystem:
1. Subscribes to `orchestrator.tick` to receive time sync
2. Reads state from upstream subsystems (e.g., GNC reads dynamics)
3. Computes its own state
4. Publishes to `telemetry.<subsystem>.state`
5. Listens on `command.uplink` for commands

## Time Management

- Orchestrator publishes ticks at configurable rate
- Each tick includes: sim_time, real_time, rate, tick_number
- Services are stateless between ticks (state stored in NATS KV)
- Checkpoint/restore via NAS service

## Fault Injection

Faults are defined in scenario JSON and executed by the orchestrator at specified sim times. Each fault targets a subsystem with a type and parameters.
