"""MCP handlers — telemetry, command, system, scenario."""

import asyncio
import json
import logging
import time
import uuid
from datetime import datetime, timezone
from typing import Any

import nats

logger = logging.getLogger("mcp.handlers")


class TelemetryHandler:
    """Fetch telemetry from NATS JetStream or direct request."""

    def __init__(self, nc: nats.NATSConnection):
        self.nc = nc
        self._cache: dict[str, dict[str, Any]] = {}

    async def get_latest(self, subsystem: str) -> dict[str, Any]:
        subject = f"telemetry.{subsystem}.state"
        try:
            msg = await self.nc.request(subject, b"", timeout=2.0)
            data = json.loads(msg.data)
            self._cache[subsystem] = data
            return data
        except asyncio.TimeoutError:
            cached = self._cache.get(subsystem)
            if cached:
                logger.warning("telemetry request timed out, returning cached %s", subsystem)
                return {"cached": True, "data": cached}
            return {"error": f"no telemetry available for {subsystem}"}

    async def get_history(self, subsystem: str, limit: int = 100) -> list[dict[str, Any]]:
        js = self.nc.jetstream()
        stream_name = f"telemetry_{subsystem}"
        try:
            stream_info = await js.get_stream_info(stream_name)
        except Exception:
            return [{"error": f"no history stream for {subsystem}"}]
        sub = await js.pull_subscribe(f"telemetry.{subsystem}.state", stream=stream_name)
        msgs = await sub.fetch(limit, timeout=3.0)
        history = []
        for msg in msgs:
            try:
                history.append(json.loads(msg.data))
            except json.JSONDecodeError:
                continue
            await msg.ack()
        await sub.unsubscribe()
        return history

    async def subscribe_telemetry(
        self, subsystem: str, callback: callable
    ) -> nats.subscription.Subscription:
        subject = f"telemetry.{subsystem}.state"
        sub = await self.nc.subscribe(subject, cb=callback)
        return sub


class CommandHandler:
    """Send commands and await responses via NATS."""

    def __init__(self, nc: nats.NATSConnection):
        self.nc = nc

    async def send_command(
        self, target: str, opcode: str, args: dict[str, Any] | None = None
    ) -> dict[str, Any]:
        cmd = {
            "id": str(uuid.uuid4()),
            "source": "mcp",
            "target": target,
            "opcode": opcode,
            "args": args or {},
            "priority": 1,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }
        payload = json.dumps(cmd).encode()
        try:
            # Publish to command.uplink and wait for response on command.downlink
            inbox = self.nc.new_inbox()
            sub = await self.nc.subscribe(inbox, max_msgs=1)

            # Subscribe to command.downlink with specific target filtering
            async def response_handler(msg):
                try:
                    resp = json.loads(msg.data)
                    if resp.get("cmd_id") == cmd["id"]:
                        await sub.unsubscribe()
                        return resp
                except json.JSONDecodeError:
                    pass

            downlink_sub = await self.nc.subscribe("command.downlink", cb=response_handler)
            await self.nc.publish("command.uplink", payload, reply=inbox)

            try:
                resp_msg = await asyncio.wait_for(sub.next_msg(), timeout=5.0)
                await downlink_sub.unsubscribe()
                return json.loads(resp_msg.data)
            except asyncio.TimeoutError:
                await downlink_sub.unsubscribe()
                return {"cmd_id": cmd["id"], "success": False, "message": "command timed out"}
        except Exception as e:
            return {"cmd_id": cmd["id"], "success": False, "message": str(e)}


class SystemHandler:
    """Collect system-wide state from NATS."""

    def __init__(self, nc: nats.NATSConnection):
        self.nc = nc

    async def get_status(self) -> dict[str, Any]:
        subsystems = [
            "dynamics", "gnc", "eps", "eclss",
            "thermal", "comms", "cdh", "crew",
        ]
        status = {}
        for sub in subsystems:
            subject = f"telemetry.{sub}.state"
            try:
                msg = await self.nc.request(subject, b"", timeout=2.0)
                data = json.loads(msg.data)
                status[sub] = {"online": True, "timestamp": data.get("timestamp")}
            except Exception:
                status[sub] = {"online": False}
        return status

    async def list_subsystems(self) -> list[dict[str, str]]:
        return [
            {"name": "dynamics", "description": "Orbital dynamics and position"},
            {"name": "gnc", "description": "Guidance, navigation, and control"},
            {"name": "eps", "description": "Electrical power system"},
            {"name": "eclss", "description": "Environmental control and life support"},
            {"name": "thermal", "description": "Thermal control system"},
            {"name": "comms", "description": "Communications and tracking"},
            {"name": "cdh", "description": "Command and data handling"},
            {"name": "crew", "description": "Crew status and activity"},
        ]


class ScenarioHandler:
    """Manage scenario lifecycle via orchestrator."""

    def __init__(self, nc: nats.NATSConnection):
        self.nc = nc

    async def get_current_scenario(self) -> dict[str, Any]:
        try:
            msg = await self.nc.request("orchestrator.scenario", b"", timeout=2.0)
            return json.loads(msg.data)
        except asyncio.TimeoutError:
            return {"error": "scenario request timed out"}

    async def set_time_scale(self, rate: float) -> dict[str, Any]:
        cmd = {
            "target": "orchestrator",
            "opcode": "SET_TIME_SCALE",
            "args": {"rate": rate},
        }
        payload = json.dumps(cmd).encode()
        try:
            msg = await self.nc.request("command.orchestrator", payload, timeout=5.0)
            return json.loads(msg.data)
        except asyncio.TimeoutError:
            return {"error": "set_time_scale request timed out"}

    async def inject_fault(
        self, subsystem: str, fault_type: str, params: dict[str, Any] | None = None
    ) -> dict[str, Any]:
        cmd = {
            "target": subsystem,
            "opcode": "INJECT_FAULT",
            "args": {"fault_type": fault_type, "params": params or {}},
        }
        payload = json.dumps(cmd).encode()
        try:
            msg = await self.nc.request("command.uplink", payload, timeout=5.0)
            return json.loads(msg.data)
        except asyncio.TimeoutError:
            return {"error": "fault injection request timed out"}
