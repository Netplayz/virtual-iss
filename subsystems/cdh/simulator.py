import asyncio
import json
import logging

from nats.aio.client import Client as NATS

from .cmd_handler import CommandHandler
from .limit_checker import LimitChecker
from .tlm_packer import pack_telemetry

logger = logging.getLogger(__name__)


class CDHSimulator:
    def __init__(self, nc: NATS) -> None:
        self.nc = nc
        self.cmd_handler = CommandHandler()
        self.limit_checker = LimitChecker()
        self._configure_limits()

    def _configure_limits(self) -> None:
        lc = self.limit_checker
        lc.add_limit("eclss", "o2_partial_kpa", 19.5, 23.0, "warning")
        lc.add_limit("eclss", "co2_partial_kpa", 0.0, 0.5, "warning")
        lc.add_limit("eclss", "cabin_temp_c", 18.0, 27.0, "warning")
        lc.add_limit("thermal", "loop_a_temp_in", -10.0, 40.0, "warning")
        lc.add_limit("eps", "bus_voltage_v", 113.0, 126.0, "critical")
        lc.add_limit("eps", "soc_pct", 15.0, 95.0, "critical")

    async def run(self) -> None:
        await self.nc.subscribe("telemetry.eclss.state", cb=self._on_tlm)
        await self.nc.subscribe("telemetry.thermal.state", cb=self._on_tlm)
        await self.nc.subscribe("telemetry.eps.state", cb=self._on_tlm)
        await self.nc.subscribe("telemetry.crew.state", cb=self._on_tlm)
        await self.nc.subscribe("orchestrator.tick", cb=self._on_tick)
        await self.nc.subscribe("command.uplink", cb=self._on_uplink)
        logger.info("C&DH simulator listening for telemetry and commands")
        await asyncio.Event().wait()

    async def _on_tick(self, msg) -> None:
        await self.publish_state()

    async def _on_tlm(self, msg) -> None:
        data = json.loads(msg.data.decode())
        subject = msg.subject
        subsystem = subject.split(".")[-2]
        packed = pack_telemetry(subsystem, data)
        events = self.limit_checker.check(subsystem, data)
        for ev in events:
            logger.warning("Limit event: %s", ev)
        if events:
            packed["alarms"] = events
        await self.nc.publish(
            f"telemetry.{subsystem}.verified",
            json.dumps(packed).encode(),
        )

    async def _on_uplink(self, msg) -> None:
        cmd = json.loads(msg.data.decode())
        valid, reason = self.cmd_handler.verify(cmd)
        if not valid:
            logger.warning("Command rejected: %s", reason)
            return
        resp = self.cmd_handler.execute(cmd)
        await self.nc.publish(
            "telemetry.cdh.cmd_response",
            json.dumps(resp).encode(),
        )

    def verify_command(self, cmd: dict) -> bool:
        valid, _ = self.cmd_handler.verify(cmd)
        return valid

    def execute_stored_sequence(self, seq_id: str) -> None:
        logger.info("Executing stored sequence %s", seq_id)
        cmd = {"command": "EXEC_SEQUENCE", "seq_id": seq_id}
        valid, reason = self.cmd_handler.verify(cmd)
        if valid:
            self.cmd_handler.execute(cmd)

    def check_limits(self, tlm: dict) -> list[dict]:
        return self.limit_checker.check(tlm.get("subsystem", ""), tlm)

    async def publish_state(self) -> None:
        state = {
            "subsystem": "cdh",
            "mode": "nominal",
            "cmd_queue_depth": len(self.cmd_handler.stored_queue),
            "limit_rules_active": len(self.limit_checker.limits),
        }
        await self.nc.publish(
            "telemetry.cdh.state",
            json.dumps(state).encode(),
        )
