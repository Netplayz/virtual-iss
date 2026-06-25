import asyncio
import json
import logging
from dataclasses import dataclass

from nats.aio.client import Client as NATS

from .active_loop import ActiveThermalLoop
from .heaters import HeaterSystem
from .radiator import Radiator

logger = logging.getLogger(__name__)


@dataclass
class ThermalState:
    loop_a_temp_in_c: float = 3.0
    loop_a_temp_out_c: float = 6.0
    loop_b_temp_in_c: float = 2.0
    loop_b_temp_out_c: float = 5.0
    flow_rate_lpm: float = 200.0
    heat_rejected_w: float = 0.0
    heater_power_w: float = 0.0
    radiator_angle_deg: float = 0.0


class ThermalSimulator:
    def __init__(self, nc: NATS) -> None:
        self.nc = nc
        self.loop = ActiveThermalLoop()
        self.radiator = Radiator()
        self.heater = HeaterSystem()
        self.tick_dt_s: float = 1.0

    async def run(self) -> None:
        await self.nc.subscribe("orchestrator.tick", cb=self._on_tick)
        await self.nc.subscribe("command.uplink", cb=self._on_uplink)
        logger.info("Thermal simulator listening for ticks and commands")
        await asyncio.Event().wait()

    async def _on_tick(self, msg) -> None:
        data = json.loads(msg.data.decode()) if msg.data else {}
        dt_s = data.get("dt_s", self.tick_dt_s)

        state = self._step(dt_s)
        await self.nc.publish(
            "telemetry.thermal.state",
            json.dumps(state.__dict__).encode(),
        )

    def _step(self, dt_s: float) -> ThermalState:
        heat_load_w = 30000.0
        environment_temp_c = -100.0

        loop_a_in, loop_a_out, flow = self.loop.update(
            heat_load_w, environment_temp_c, dt_s
        )

        heat_rejected = self.radiator.reject_heat(loop_a_out, environment_temp_c)
        radiator_temp_c = environment_temp_c + 50.0
        heater_power = self.heater.regulate(loop_a_in, 4.0, dt_s)

        return ThermalState(
            loop_a_temp_in_c=loop_a_in,
            loop_a_temp_out_c=loop_a_out,
            loop_b_temp_in_c=self.loop.loop_b_temp_in,
            loop_b_temp_out_c=self.loop.loop_b_temp_out,
            flow_rate_lpm=flow,
            heat_rejected_w=heat_rejected,
            heater_power_w=heater_power,
            radiator_angle_deg=self.radiator.angle_deg,
        )

    async def _on_uplink(self, msg) -> None:
        cmd = json.loads(msg.data.decode())
        logger.info("Received Thermal command: %s", cmd.get("command"))
