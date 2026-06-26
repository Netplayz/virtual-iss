import asyncio
import json
import logging
from dataclasses import dataclass, field

from nats.aio.client import Client as NATS

from .cabin_atmo import CabinAtmosphere
from .co2_scrub import CDRAScrubber
from .o2_gen import ElectrolysisO2Gen
from .water_recovery import WaterRecovery

logger = logging.getLogger(__name__)


@dataclass
class ECLSSFaults:
    o2_gen_failed: bool = False
    co2_scrub_failed: bool = False
    water_recovery_failed: bool = False
    cabin_leak_rate_kpa_s: float = 0.0

@dataclass
class ECLSSState:
    o2_partial_kpa: float = 21.2
    co2_partial_kpa: float = 0.04
    cabin_temp_c: float = 22.0
    cabin_humidity_pct: float = 50.0
    cabin_pressure_kpa: float = 101.3
    o2_produced_kg: float = 0.0
    co2_removed_kg: float = 0.0
    water_recovered_l: float = 0.0
    power_w: float = 0.0
    faults: ECLSSFaults = field(default_factory=ECLSSFaults)


class ECLSSSimulator:
    def __init__(self, nc: NATS) -> None:
        self.nc = nc
        self.cabin = CabinAtmosphere()
        self.o2_gen = ElectrolysisO2Gen()
        self.co2_scrubber = CDRAScrubber()
        self.water_recovery = WaterRecovery()
        self.crew_count: int = 4
        self.tick_dt_s: float = 1.0
        self.faults: ECLSSFaults = ECLSSFaults()

    async def run(self) -> None:
        await self.nc.subscribe("orchestrator.tick", cb=self._on_tick)
        await self.nc.subscribe("command.uplink", cb=self._on_uplink)
        logger.info("ECLSS simulator listening for ticks and commands")
        await asyncio.Event().wait()

    async def _on_tick(self, msg) -> None:
        data = json.loads(msg.data.decode()) if msg.data else {}
        dt_s = data.get("dt_s", self.tick_dt_s)

        state = self._step(dt_s)
        await self.nc.publish(
            "telemetry.eclss.state",
            json.dumps(state.__dict__).encode(),
        )

    def _step(self, dt_s: float) -> ECLSSState:
        cabin_state = self.cabin.get_state()
        o2_partial = cabin_state["o2_partial_kpa"]
        co2_partial = cabin_state["co2_partial_kpa"]

        o2_produced = 0.0
        if not self.faults.o2_gen_failed and o2_partial < 21.0:
            power_w = self.o2_gen.get_power_demand()
            o2_produced = self.o2_gen.produce_o2(power_w, o2_partial, dt_s)

        co2_removed = 0.0
        if not self.faults.co2_scrub_failed and co2_partial > 0.4:
            co2_removed = self.co2_scrubber.scrub_co2(
                co2_partial, cabin_state["cabin_pressure_kpa"], dt_s
            )
            self.co2_scrubber.regeneration_cycle()

        recovered = 0.0
        if not self.faults.water_recovery_failed:
            w_rec = self.water_recovery
            urine_l = crew_count_urine(self.crew_count, dt_s)
            w_rec.process_urine(urine_l)
            humidity = cabin_state["humidity_pct"]
            temp = cabin_state["cabin_temp_c"]
            recovered = w_rec.process_humidity(humidity, temp)

        cabin_update_kwargs = {
            "o2_produced": o2_produced,
            "co2_removed": co2_removed,
        }
        if self.faults.cabin_leak_rate_kpa_s > 0:
            cabin_update_kwargs["leak_rate"] = self.faults.cabin_leak_rate_kpa_s

        self.cabin.update(dt_s, self.crew_count, **cabin_update_kwargs)

        cabin_state = self.cabin.get_state()
        state = ECLSSState(
            o2_partial_kpa=cabin_state["o2_partial_kpa"],
            co2_partial_kpa=cabin_state["co2_partial_kpa"],
            cabin_temp_c=cabin_state["cabin_temp_c"],
            cabin_humidity_pct=cabin_state["humidity_pct"],
            cabin_pressure_kpa=cabin_state["cabin_pressure_kpa"],
            o2_produced_kg=o2_produced,
            co2_removed_kg=co2_removed,
            water_recovered_l=recovered,
            power_w=self.o2_gen.get_power_demand(),
            faults=self.faults,
        )
        return state

    async def _on_uplink(self, msg) -> None:
        cmd = json.loads(msg.data.decode())

        target = cmd.get("target", "")
        if target not in ("eclss", "all"):
            return

        action = cmd.get("action", cmd.get("opcode", ""))
        logger.info("ECLSS command: %s", action)

        if action == "SET_FAULT":
            payload = cmd.get("payload", cmd.get("params", {}))
            if isinstance(payload, str):
                try:
                    payload = json.loads(payload)
                except json.JSONDecodeError:
                    pass
            fault_type = payload.get("type") if isinstance(payload, dict) else payload
            params = payload.get("params", {}) if isinstance(payload, dict) else {}
            await self._inject_fault(fault_type, params)

        elif action == "CLEAR_FAULT":
            payload = cmd.get("payload", cmd.get("params", {}))
            if isinstance(payload, str):
                try:
                    payload = json.loads(payload)
                except json.JSONDecodeError:
                    pass
            fault_type = payload.get("type") if isinstance(payload, dict) else payload
            self._clear_fault(fault_type)

    async def _inject_fault(self, fault_type: str, params: dict) -> None:
        if fault_type == "o2_gen_fault":
            self.faults.o2_gen_failed = True
            logger.warning("O2 generator fault injected")
        elif fault_type == "co2_scrub_fault":
            self.faults.co2_scrub_failed = True
            logger.warning("CO2 scrubber fault injected")
        elif fault_type == "water_recovery_fault":
            self.faults.water_recovery_failed = True
            logger.warning("Water recovery fault injected")
        elif fault_type == "cabin_leak":
            rate = params.get("leak_rate_kpa_s", 0.01)
            self.faults.cabin_leak_rate_kpa_s = rate
            logger.warning("Cabin leak injected: %f kPa/s", rate)
        else:
            logger.warning("Unknown ECLSS fault type: %s", fault_type)

    def _clear_fault(self, fault_type: str) -> None:
        if fault_type == "o2_gen_fault":
            self.faults.o2_gen_failed = False
            logger.info("O2 generator fault cleared")
        elif fault_type == "co2_scrub_fault":
            self.faults.co2_scrub_failed = False
            logger.info("CO2 scrubber fault cleared")
        elif fault_type == "water_recovery_fault":
            self.faults.water_recovery_failed = False
            logger.info("Water recovery fault cleared")
        elif fault_type == "cabin_leak":
            self.faults.cabin_leak_rate_kpa_s = 0.0
            logger.info("Cabin leak cleared")
        else:
            self.faults = ECLSSFaults()
            logger.info("All ECLSS faults cleared")


def crew_count_urine(crew: int, dt_s: float) -> float:
    return crew * 0.0015 * dt_s
