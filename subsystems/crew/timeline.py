import logging
from dataclasses import dataclass

logger = logging.getLogger(__name__)


@dataclass
class Activity:
    name: str
    duration_s: float
    crew_count: int
    consumables_rate: float = 1.0


_DEFAULT_TIMELINE: list[Activity] = [
    Activity("sleep", 28800.0, 4, 0.3),
    Activity("exercise", 7200.0, 4, 2.0),
    Activity("maintenance", 14400.0, 2, 1.0),
    Activity("science", 21600.0, 3, 1.0),
    Activity("meal", 7200.0, 4, 0.5),
    Activity("leisure", 7200.0, 4, 0.5),
    Activity("standby", 3600.0, 4, 0.8),
]


class ActivityTimeline:
    def __init__(self) -> None:
        self.activities: list[Activity] = list(_DEFAULT_TIMELINE)
        self._total_duration: float = sum(a.duration_s for a in self.activities)

    def get_current_activity(self, sim_time: float) -> Activity | None:
        t = sim_time % self._total_duration
        cumulative = 0.0
        for act in self.activities:
            cumulative += act.duration_s
            if t < cumulative:
                return act
        return None

    def get_next_activity(self, sim_time: float) -> Activity | None:
        t = sim_time % self._total_duration
        cumulative = 0.0
        for i, act in enumerate(self.activities):
            cumulative += act.duration_s
            if t < cumulative:
                nxt = self.activities[(i + 1) % len(self.activities)]
                return nxt
        return None

    @staticmethod
    def fallback_activity() -> Activity:
        return Activity("standby", 3600.0, 4, 0.8)
