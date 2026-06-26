import asyncio
import json
import logging
import os
import time
from datetime import datetime, timezone
from typing import Any

from aiohttp import web

from .scenario import Scenario

logger = logging.getLogger(__name__)

DEFAULT_SCENARIO_PATH = os.environ.get(
    "SCENARIO_PATH",
    "configs/scenarios/nominal_ops.json",
)
TICK_RATE_HZ = 10.0
TICK_INTERVAL_S = 1.0 / TICK_RATE_HZ


class Orchestrator:
    def __init__(self, nc: Any) -> None:
        self.nc = nc
        self.sim_time: float = 0.0
        self.tick_number: int = 0
        self.time_scale: float = 1.0
        self.paused: bool = True
        self.running: bool = True
        self.scenario: Scenario | None = None
        self._last_wall: float = time.monotonic()
        self._last_tick_wall: float = time.monotonic()

    async def run(self) -> None:
        logger.info("Orchestrator starting")
        await self._load_scenario(DEFAULT_SCENARIO_PATH)

        app = web.Application()
        app.add_routes(
            [
                web.get("/api/v1/status", self._handle_status),
                web.post("/api/v1/scenario/load", self._handle_load_scenario),
                web.post("/api/v1/control/pause", self._handle_pause),
                web.post("/api/v1/control/resume", self._handle_resume),
                web.post("/api/v1/control/set_rate", self._handle_set_rate),
            ]
        )
        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, "0.0.0.0", 8300)
        await site.start()
        logger.info("HTTP API listening on :8300")

        await self.nc.subscribe("command.orchestrator", cb=self._on_command)

        self._last_wall = time.monotonic()
        self._last_tick_wall = time.monotonic()

        while self.running:
            now = time.monotonic()
            wall_elapsed = now - self._last_wall
            self._last_wall = now

            if not self.paused and self.scenario is not None:
                dt = wall_elapsed * self.time_scale
                self.sim_time += dt
                self.tick_number += 1

                tick_msg = {
                    "sim_time_sec": round(self.sim_time, 3),
                    "real_time_sec": round(wall_elapsed, 3),
                    "rate": self.time_scale,
                    "tick_number": self.tick_number,
                    "scenario_id": self.scenario.id,
                }
                await self.nc.publish("orchestrator.tick", json.dumps(tick_msg).encode())

                status_msg = {
                    "sim_time_sec": self.sim_time,
                    "tick_number": self.tick_number,
                    "time_scale": self.time_scale,
                    "paused": self.paused,
                    "scenario_id": self.scenario.id,
                    "timestamp": datetime.now(timezone.utc).isoformat(),
                }
                await self.nc.publish("orchestrator.status", json.dumps(status_msg).encode())

                faults = self.scenario.check_faults(self.sim_time)
                for fault in faults:
                    logger.warning(
                        "Injecting fault at t=%.1f: %s/%s",
                        fault.time_sec,
                        fault.subsystem,
                        fault.type,
                    )
                    await self.nc.publish(
                        "event.anomaly",
                        json.dumps(
                            {
                                "time_sec": self.sim_time,
                                "subsystem": fault.subsystem,
                                "type": fault.type,
                                "params": fault.params,
                            }
                        ).encode(),
                    )

                if self.sim_time >= self.scenario.duration_sec:
                    logger.info("Scenario '%s' complete. Pausing.", self.scenario.id)
                    self.paused = True

            processing_time = time.monotonic() - now
            sleep_time = max(0.0, TICK_INTERVAL_S - processing_time)
            await asyncio.sleep(sleep_time)

    async def _load_scenario(self, path: str) -> None:
        scenario = Scenario.load(path)
        self.scenario = scenario
        self.sim_time = 0.0
        self.tick_number = 0
        self.time_scale = scenario.time_scale
        self.paused = True
        logger.info("Loaded scenario: %s (id=%s, duration=%.0fs)", scenario.name, scenario.id, scenario.duration_sec)

        await self.nc.publish(
            "orchestrator.scenario",
            json.dumps(
                {
                    "id": scenario.id,
                    "name": scenario.name,
                    "description": scenario.description,
                    "initial_orbit": scenario.initial_orbit,
                    "subsystems": scenario.subsystems,
                    "duration_sec": scenario.duration_sec,
                    "time_scale": scenario.time_scale,
                }
            ).encode(),
        )

    async def _on_command(self, msg: Any) -> None:
        try:
            cmd = json.loads(msg.data.decode())
        except json.JSONDecodeError:
            logger.error("Invalid command message")
            return

        opcode = cmd.get("opcode", "")
        args = cmd.get("args", {})
        logger.info("Received command: %s %s", opcode, args)

        if opcode == "LOAD_SCENARIO":
            path = args.get("path", DEFAULT_SCENARIO_PATH)
            await self._load_scenario(path)
        elif opcode == "SET_TIME_SCALE":
            self.time_scale = float(args.get("rate", 1.0))
            logger.info("Time scale set to %.2f", self.time_scale)
        elif opcode == "PAUSE":
            self.paused = True
            logger.info("Simulation paused")
        elif opcode == "RESUME":
            self.paused = False
            self._last_wall = time.monotonic()
            logger.info("Simulation resumed")
        elif opcode == "STOP":
            logger.info("Simulation stopping")
            self.running = False
        else:
            logger.warning("Unknown command opcode: %s", opcode)

    async def _handle_status(self, request: web.Request) -> web.Response:
        return web.json_response(
            {
                "sim_time_sec": self.sim_time,
                "tick_number": self.tick_number,
                "time_scale": self.time_scale,
                "paused": self.paused,
                "scenario_id": self.scenario.id if self.scenario else None,
                "scenario_name": self.scenario.name if self.scenario else None,
            }
        )

    async def _handle_load_scenario(self, request: web.Request) -> web.Response:
        body = await request.json()
        path = body.get("path", DEFAULT_SCENARIO_PATH)
        await self._load_scenario(path)
        return web.json_response({"status": "ok", "scenario_id": self.scenario.id if self.scenario else None})

    async def _handle_pause(self, request: web.Request) -> web.Response:
        self.paused = True
        return web.json_response({"status": "ok", "paused": True})

    async def _handle_resume(self, request: web.Request) -> web.Response:
        self.paused = False
        self._last_wall = time.monotonic()
        return web.json_response({"status": "ok", "paused": False})

    async def _handle_set_rate(self, request: web.Request) -> web.Response:
        body = await request.json()
        self.time_scale = float(body.get("rate", 1.0))
        return web.json_response({"status": "ok", "time_scale": self.time_scale})
