import asyncio
import json
import logging
from dataclasses import dataclass, field

from nats.aio.client import Client as NATS

from .consumables import ConsumablesTracker
from .timeline import ActivityTimeline

logger = logging.getLogger(__name__)


@dataclass
class CrewState:
    sim_time: float = 0.0
    crew_count: int = 4
    current_activity: str = "standby"
    next_activity: str = "standby"
    food_remaining_kg: float = 120.0
    water_remaining_l: float = 200.0
    o2_remaining_kg: float = 50.0
    co2_produced_kg: float = 0.0


class CrewSimulator:
    def __init__(self, nc: NATS) -> None:
        self.nc = nc
        self.crew_count: int = 4
        self.sim_time: float = 0.0
        self.timeline = ActivityTimeline()
        self.consumables = ConsumablesTracker()
        self.tick_dt_s: float = 1.0

    async def run(self) -> None:
        await self.nc.subscribe("orchestrator.tick", cb=self._on_tick)
        await self.nc.subscribe("command.uplink", cb=self._on_uplink)
        logger.info("Crew simulator listening for ticks and commands")
        await asyncio.Event().wait()

    async def _on_tick(self, msg) -> None:
        data = json.loads(msg.data.decode()) if msg.data else {}
        dt_s = data.get("dt_s", self.tick_dt_s)

        state = self._step(dt_s)
        await self.publish_state(state)

    def _step(self, dt_s: float) -> CrewState:
        self.sim_time += dt_s

        current = self.timeline.get_current_activity(self.sim_time)
        next_act = self.timeline.get_next_activity(self.sim_time)

        activity = current or ActivityTimeline._fallback_activity()
        self.consumables.consume(self.crew_count, dt_s)

        remaining = self.consumables.get_remaining()
        return CrewState(
            sim_time=self.sim_time,
            crew_count=self.crew_count,
            current_activity=activity.name,
            next_activity=next_act.name if next_act else activity.name,
            food_remaining_kg=remaining["food_kg"],
            water_remaining_l=remaining["water_l"],
            o2_remaining_kg=remaining["o2_kg"],
            co2_produced_kg=self.crew_count * (1.0 / 86400.0) * dt_s,
        )

    async def publish_state(self, state: CrewState) -> None:
        await self.nc.publish(
            "telemetry.crew.state",
            json.dumps({
                "sim_time": state.sim_time,
                "crew_count": state.crew_count,
                "current_activity": state.current_activity,
                "next_activity": state.next_activity,
                "food_remaining_kg": state.food_remaining_kg,
                "water_remaining_l": state.water_remaining_l,
                "o2_remaining_kg": state.o2_remaining_kg,
                "co2_produced_kg": state.co2_produced_kg,
            }).encode(),
        )

    async def _on_uplink(self, msg) -> None:
        cmd = json.loads(msg.data.decode())
        logger.info("Received Crew command: %s", cmd.get("command"))
