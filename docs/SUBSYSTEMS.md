# Subsystem Reference

## Dynamics (Rust)
Orbit propagation via SGP4 with J2 perturbations. Rigid-body 6-DOF attitude dynamics. Environmental torque models (gravity gradient, aerodynamic, magnetic, solar radiation pressure). ISS vehicle config with mass properties, solar array geometry, CMG cluster.

## GN&C (Rust)
Sensors: star tracker, gyro, sun sensor, magnetometer, GPS. Multiplicative Extended Kalman Filter for attitude estimation. Quaternion feedback PID control. Pyramid CMG steering law with singularity avoidance.

## EPS (Go)
Triple-junction GaAs solar array I-V curves. Li-Ion battery SOC/capacity model. 120V DC bus with DDCU conversion. Priority-based load shedding with 8 priority levels.

## ECLSS (Python)
Electrolysis O₂ generation, CDRA CO₂ scrubbing with bed cycling. Urine/humidity water recovery. Cabin atmosphere control (pressure, temp, humidity, trace contaminants).

## Thermal (Python)
Dual active ammonia loops (Loop A/B). Variable-conductance radiator positioning. Heater zone control with PID regulation. Passive MLI model.

## Comms (Go)
TDRSS satellite coverage prediction. S-band command/telemetry links with link budget. Ku-band video downlink. Signal latency computation.

## C&DH (Python)
cFE-style command verification and routing. Telemetry packing with sequence numbers. Limit checking with configurable red/yellow thresholds. Stored command sequence execution.

## Crew (Python)
Crew activity timeline with configurable schedules. Resource consumption tracking (O₂, CO₂, H₂O, food). Sleep/wake cycling. Multi-crew support.

## NAS (Go)
Telemetry stream recording to JSONL files. Time-indexed replay at variable rate. Object store via NATS JetStream KV. HTTP REST API for query/download.

## MCP (Python)
Model Context Protocol server exposing simulation as tools/resources. AI agents can query telemetry, send commands, inject faults, and manage scenarios. OpenAPI 3.1 + SSE streaming.
