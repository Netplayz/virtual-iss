import json
import logging
from dataclasses import dataclass, field
from typing import Any

logger = logging.getLogger(__name__)


@dataclass
class FaultEvent:
    time_sec: float
    subsystem: str
    type: str
    params: dict[str, Any]


@dataclass
class Scenario:
    id: str
    name: str
    description: str
    initial_orbit: dict[str, Any]
    subsystems: list[str]
    duration_sec: float
    time_scale: float
    faults: list[FaultEvent] = field(default_factory=list)
    crew_count: int = 0
    initial_beta_angle_deg: float = 0.0

    @classmethod
    def load(cls, path: str) -> "Scenario":
        with open(path) as f:
            data = json.load(f)

        faults = []
        for f_entry in data.get("faults", []):
            faults.append(
                FaultEvent(
                    time_sec=f_entry["time_sec"],
                    subsystem=f_entry["subsystem"],
                    type=f_entry["type"],
                    params=f_entry.get("params", {}),
                )
            )

        return cls(
            id=data["id"],
            name=data.get("name", ""),
            description=data.get("description", ""),
            initial_orbit=data.get("initial_orbit", {}),
            subsystems=data.get("subsystems", []),
            duration_sec=data.get("duration_sec", 5400.0),
            time_scale=data.get("time_scale", 1.0),
            faults=faults,
            crew_count=data.get("crew_count", 0),
            initial_beta_angle_deg=data.get("initial_beta_angle_deg", 0.0),
        )

    def get_subsystem_list(self) -> list[str]:
        return self.subsystems

    def get_initial_conditions(self) -> dict[str, Any]:
        return {
            "orbit": dict(self.initial_orbit),
            "beta_angle_deg": self.initial_beta_angle_deg,
            "crew_count": self.crew_count,
        }

    def check_faults(self, sim_time: float) -> list[FaultEvent]:
        triggered = []
        for fault in self.faults:
            if abs(sim_time - fault.time_sec) < 0.01:
                triggered.append(fault)
        return triggered
