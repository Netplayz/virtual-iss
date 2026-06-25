# Scenario Guide

## Format

Scenarios are JSON files in `configs/scenarios/`. Each includes initial conditions, active subsystems, fault timeline, and time scale.

```json
{
  "id": "scenario_name",
  "name": "Human Readable Name",
  "initial_orbit": { ... },
  "subsystems": ["dynamics", "gnc", "eps", ...],
  "duration_sec": 5400.0,
  "time_scale": 1.0,
  "faults": [
    { "time_sec": 600.0, "subsystem": "eps", "type": "...", "params": {} }
  ]
}
```

## Available

- **nominal_ops** — Standard 90-min orbit, all subsystems, crew of 7, no faults
- **load_shed_test** — Solar array failure at T+600s, verify automatic load shedding

## Creating

1. Create JSON in `configs/scenarios/`
2. POST to orchestrator: `POST /api/v1/scenario/load { "scenario_id": "my_scenario" }`
3. Start with `POST /api/v1/control/resume`

## Fault Types

| Fault | Subsystem | Params |
|-------|-----------|--------|
| `solar_array_failure` | eps | `panel_id`, `degradation` |
| `battery_failure` | eps | `battery_id` |
| `o2_gen_fault` | eclss | `reduction_pct` |
| `co2_scrub_fault` | eclss | `bed_stuck` |
| `sensor_failure` | gnc | `sensor`, `mode` |
| `attitude_anomaly` | gnc | `torque_bias_nm` |

## API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/scenario/load` | POST | Load scenario by ID |
| `/api/v1/scenario/current` | GET | Current scenario info |
| `/api/v1/control/pause` | POST | Pause simulation |
| `/api/v1/control/resume` | POST | Resume simulation |
| `/api/v1/control/set_rate` | POST | Set time scale |
