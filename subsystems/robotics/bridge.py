#!/usr/bin/env python3
"""
bridge.py — Robotics daemon to NATS bridge.

Spawns the roboticsd binary (or reads from a pipe), parses JSON telemetry
from stdout, and publishes to telemetry.robotics.state on NATS.
Subscribes to command.uplink and forwards commands via stdin JSON lines.
"""

import asyncio
import json
import logging
import os
import sys

logging.basicConfig(
    level=logging.INFO,
    format="[ROBOTICS-BRIDGE] %(levelname)s: %(message)s",
)
logger = logging.getLogger("robotics_bridge")

NATS_URL = os.environ.get("NATS_URL", "nats://localhost:4222")
ROBOTICS_BINARY = os.environ.get("ROBOTICS_BINARY", "./build/roboticsd")

TELEMETRY_SUBJECT = "telemetry.robotics.state"
COMMAND_SUBJECT = "command.uplink"
TICK_SUBJECT = "orchestrator.tick"


class RoboticsBridge:
    def __init__(self, process: asyncio.subprocess.Process):
        self.proc = process
        self.nc = None

    async def connect_nats(self):
        try:
            import nats
            self.nc = await nats.connect(NATS_URL)
            logger.info("Connected to NATS at %s", NATS_URL)
            await self.nc.subscribe(COMMAND_SUBJECT, cb=self._on_command)
            logger.info("Subscribed to %s", COMMAND_SUBJECT)
        except Exception as e:
            logger.error("NATS connection failed: %s", e)
            self.nc = None

    async def _on_command(self, msg):
        try:
            data = json.loads(msg.data.decode())
            line = json.dumps(data) + "\n"
            if self.proc.stdin and not self.proc.stdin.is_closing():
                self.proc.stdin.write(line.encode())
                await self.proc.stdin.drain()
                logger.debug("Forwarded command to robotics: %s", data.get("opcode", ""))
        except Exception as e:
            logger.error("Command forward error: %s", e)

    async def read_telemetry(self):
        while True:
            line = await self.proc.stdout.readline()
            if not line:
                logger.info("Robotics process closed stdout")
                break
            line = line.decode().strip()
            if not line:
                continue
            try:
                parsed = json.loads(line)
            except json.JSONDecodeError:
                continue
            if self.nc:
                await self.nc.publish(TELEMETRY_SUBJECT, line.encode())
            else:
                status = parsed.get("state", "")
                mode = parsed.get("mode", "")
                power = parsed.get("power_w", 0)
                print(f"[ROBOTICS] state={status} mode={mode} power={power}W")

    async def run(self):
        tasks = [asyncio.create_task(self.read_telemetry())]
        if self.nc:
            async def keepalive():
                while True:
                    await asyncio.sleep(1)
            tasks.append(asyncio.create_task(keepalive()))
        await self.proc.wait()
        logger.info("Robotics process exited: %d", self.proc.returncode)


async def main():
    logger.info("=" * 60)
    logger.info("Robotics NATS Bridge v1.0")
    logger.info("NATS URL: %s", NATS_URL)
    logger.info("=" * 60)

    if not sys.stdin.isatty():
        logger.info("Running in pipe mode")
        class PipeProc:
            class StdoutWrapper:
                async def readline(self):
                    line = sys.stdin.buffer.readline()
                    if not line:
                        return b""
                    return line
            def __init__(self):
                self.stdout = self.StdoutWrapper()
                self.returncode = 0
            async def wait(self):
                while sys.stdin.buffer.read(1024):
                    pass
                return 0
        proc = PipeProc()
    else:
        logger.info("Spawning roboticsd: %s", ROBOTICS_BINARY)
        proc = await asyncio.create_subprocess_exec(
            ROBOTICS_BINARY,
            stdout=asyncio.subprocess.PIPE,
            stdin=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        async def log_stderr():
            while True:
                line = await proc.stderr.readline()
                if not line:
                    break
                sys.stderr.write(line.decode())
                sys.stderr.flush()
        asyncio.create_task(log_stderr())

    bridge = RoboticsBridge(proc)
    await bridge.connect_nats()
    await bridge.run()


if __name__ == "__main__":
    asyncio.run(main())
