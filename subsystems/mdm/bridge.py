#!/usr/bin/env python3
"""
bridge.py — 1553-to-NATS Bridge for MDM Flight Software

Connects the C MDM process (communicating via stdin/stdout
using MIL-STD-1553 frame format) to the Virtual ISS NATS message bus.

The bridge:
  1. Reads 1553 bus frames from the MDM's stdout
  2. Converts them to typed JSON NATS messages
  3. Publishes telemetry to 'telemetry.mdm.*'
  4. Subscribes to 'command.uplink' and forwards commands
     as 1553 frames to the MDM's stdin

Frame format:
  [2 bytes: frame length][N bytes: BUS_FRAME data]

Usage:
  ./mdm 1 0x12 | python3 bridge.py
  (or use 'make bridge')
"""

import asyncio
import json
import logging
import os
import struct
import sys
from dataclasses import dataclass, field, asdict
from typing import Optional

logging.basicConfig(
    level=logging.INFO,
    format="[BRIDGE] %(levelname)s: %(message)s",
)
logger = logging.getLogger("bridge")

# ---------------------------------------------------------------------------
# NATS subjects (mirror bus/subjects.go)
# ---------------------------------------------------------------------------
TMD_MDM_STATE = "telemetry.mdm.state"
TMD_MDM_BUS   = "telemetry.mdm.bus1553"
TMD_MDM_HEALTH = "telemetry.mdm.health"
CMD_UPLINK    = "command.uplink"
ORCH_TICK     = "orchestrator.tick"

NATS_URL = os.environ.get("NATS_URL", "nats://localhost:4222")

# ---------------------------------------------------------------------------
# 1553 Frame constants (matches mdm.h)
# ---------------------------------------------------------------------------
BUS_FRAME_HEADER_SIZE = 6  # sync(2) + cmd(2) + status(2) + word_count(2)
BUS_DATA_MAX = 32
BUS_FRAME_SIZE = BUS_FRAME_HEADER_SIZE + (BUS_DATA_MAX * 2) + 2  # + crc


@dataclass
class BusFrame:
    """Represents a MIL-STD-1553 bus frame (matches C BUS_FRAME struct)."""
    sync_pattern: int = 0xEB90
    command_word: int = 0
    status_word: int = 0
    data_word_count: int = 0
    data: list = field(default_factory=list)
    crc: int = 0

    @classmethod
    def from_bytes(cls, raw: bytes) -> "BusFrame":
        """Parse a binary BUS_FRAME from the MDM process."""
        if len(raw) < BUS_FRAME_HEADER_SIZE + 2:
            raise ValueError("Frame too short")

        sync = struct.unpack_from(">H", raw, 0)[0]
        cmd  = struct.unpack_from(">H", raw, 2)[0]
        sts  = struct.unpack_from(">H", raw, 4)[0]
        wcnt = struct.unpack_from(">H", raw, 6)[0]

        if wcnt > BUS_DATA_MAX:
            wcnt = BUS_DATA_MAX

        data = []
        for i in range(wcnt):
            offset = 8 + (i * 2)
            if offset + 2 <= len(raw):
                data.append(struct.unpack_from(">H", raw, offset)[0])

        crc_offset = 8 + (wcnt * 2)
        crc = 0
        if crc_offset + 2 <= len(raw):
            crc = struct.unpack_from(">H", raw, crc_offset)[0]

        return cls(
            sync_pattern=sync,
            command_word=cmd,
            status_word=sts,
            data_word_count=wcnt,
            data=data,
            crc=crc,
        )

    def to_bytes(self) -> bytes:
        """Serialize frame to binary for sending to MDM stdin."""
        buf = struct.pack(">HHHH", self.sync_pattern, self.command_word,
                          self.status_word, self.data_word_count)
        for w in self.data:
            buf += struct.pack(">H", w & 0xFFFF)
        # Pad to full frame size
        while len(buf) < BUS_FRAME_SIZE - 2:
            buf += struct.pack(">H", 0)
        buf += struct.pack(">H", self.crc)
        return buf


@dataclass
class MDMTelemetry:
    """Decoded MDM telemetry packet (matches tlm_format_* payloads)."""
    packet_type: int = 0
    mdm_id: int = 0
    bus_address: int = 0
    mode: int = 0
    uptime_seconds: int = 0
    tick_count: int = 0
    cmd_received: int = 0
    status_flags: int = 0
    error_flags: int = 0
    warning_flags: int = 0
    cpu_load_pct: int = 0
    stack_remaining: int = 0
    # 1553 bus status
    bus_a_active: bool = False
    bus_a_errors: int = 0
    bus_a_messages: int = 0
    bus_b_active: bool = False
    bus_b_errors: int = 0
    bus_b_messages: int = 0
    # Health
    fw_version: str = "0.0.0"
    active_sequences: int = 0
    internal_temp: int = 0


class MDMBridge:
    """Connects MDM process ↔ NATS message bus."""

    def __init__(self, mdm_process: asyncio.subprocess.Process):
        self.mdm = mdm_process
        self.nc: Optional[asyncio.Queue] = None  # replaced with NATS conn
        self.buffer = b""
        self.tick_sub: Optional = None
        self.cmd_sub: Optional = None
        self.running = True

    async def connect_nats(self):
        """Connect to NATS and set up subscriptions."""
        try:
            import nats
            self.nc = await nats.connect(NATS_URL)
            logger.info("Connected to NATS at %s", NATS_URL)

            # Subscribe to command uplink
            self.cmd_sub = await self.nc.subscribe(
                CMD_UPLINK, cb=self._on_command
            )
            logger.info("Subscribed to %s", CMD_UPLINK)

            # Subscribe to orchestrator tick for timing
            self.tick_sub = await self.nc.subscribe(
                ORCH_TICK, cb=self._on_tick
            )
            logger.info("Subscribed to %s", ORCH_TICK)

        except Exception as e:
            logger.error("NATS connection failed: %s", e)
            logger.warning("Running in standalone mode (no NATS)")
            self.nc = None

    async def _on_command(self, msg):
        """Forward a command from NATS to the MDM process as a 1553 frame."""
        try:
            data = json.loads(msg.data.decode())

            frame = BusFrame()
            # Build a BC-to-RT command frame
            rt_addr = data.get("target", 0x12)
            cmd_code = data.get("opcode", 0)
            frame.command_word = (rt_addr << 11) | (0x01 << 5) | 0x01
            frame.data = [cmd_code, data.get("cmd_id", 0) & 0xFFFF]
            frame.data_word_count = len(frame.data)
            frame.crc = 0xAAAA  # simplified

            raw = frame.to_bytes()
            frame_len = len(raw)
            header = struct.pack(">H", frame_len)

            if self.mdm.stdin and not self.mdm.stdin.is_closing():
                self.mdm.stdin.write(header + raw)
                await self.mdm.stdin.drain()
                logger.debug("Sent 1553 frame to MDM: opcode=0x%02X", cmd_code)

        except Exception as e:
            logger.error("Command forwarding error: %s", e)

    async def _on_tick(self, msg):
        """Orchestrator tick - could drive MDM timing in sim mode."""
        pass

    async def read_mdm_output(self):
        """Continuously read 1553 frames from the MDM process stdout."""
        while self.running:
            try:
                # Read frame length header
                header = await self.mdm.stdout.readexactly(2)
            except (asyncio.IncompleteReadError, ConnectionError):
                logger.info("MDM process closed stdout")
                break

            frame_len = struct.unpack(">H", header)[0]
            if frame_len > 4096:
                logger.warning("Skipping oversized frame: %d bytes", frame_len)
                continue

            try:
                raw = await self.mdm.stdout.readexactly(frame_len)
            except (asyncio.IncompleteReadError, ConnectionError):
                logger.info("MDM process closed stdout (partial frame)")
                break

            try:
                frame = BusFrame.from_bytes(raw)
                await self._process_frame(frame)
            except Exception as e:
                logger.error("Frame parse error: %s", e)

    async def _process_frame(self, frame: BusFrame):
        """Process a decoded 1553 frame and publish to NATS."""
        # Extract telemetry from the frame data words
        # The data payload contains a TLM_PACKET
        if frame.data_word_count < 2:
            return

        raw_data = b""
        for w in frame.data:
            raw_data += struct.pack(">H", w & 0xFFFF)

        if len(raw_data) < 1:
            return

        packet_type = raw_data[0] if raw_data else 0

        tlm = MDMTelemetry(packet_type=packet_type)

        # Parse based on packet type
        try:
            if packet_type == 0x01 and len(raw_data) >= 22:
                # MDM state telemetry
                tlm.mdm_id = raw_data[1]
                tlm.bus_address = raw_data[2]
                tlm.mode = raw_data[3]
                tlm.uptime_seconds = struct.unpack(">I", raw_data[4:8])[0]
                tlm.tick_count = struct.unpack(">I", raw_data[8:12])[0]
                tlm.cmd_received = struct.unpack(">I", raw_data[12:16])[0]
                tlm.status_flags = raw_data[16]
                tlm.error_flags = raw_data[17]
                tlm.warning_flags = raw_data[18]
                tlm.cpu_load_pct = raw_data[19]
                tlm.stack_remaining = struct.unpack(">H", raw_data[20:22])[0]

            elif packet_type == 0x02 and len(raw_data) >= 14:
                # 1553 bus status
                tlm.bus_a_active = bool(raw_data[1])
                tlm.bus_a_errors = raw_data[2]
                tlm.bus_a_messages = struct.unpack(">H", raw_data[3:5])[0]
                tlm.bus_b_active = bool(raw_data[5])
                tlm.bus_b_errors = raw_data[6]
                tlm.bus_b_messages = struct.unpack(">H", raw_data[7:9])[0]

            elif packet_type == 0x03 and len(raw_data) >= 14:
                # Health and status
                tlm.fw_version = f"{raw_data[1]}.{raw_data[2]}.{raw_data[3]}"
                tlm.active_sequences = raw_data[4]
                tlm.internal_temp = raw_data[5]

        except (IndexError, struct.error) as e:
            logger.debug("Packet parse warning: %s", e)

        # Publish to NATS if connected
        if self.nc:
            subject = {
                0x01: TMD_MDM_STATE,
                0x02: TMD_MDM_BUS,
                0x03: TMD_MDM_HEALTH,
            }.get(packet_type, TMD_MDM_STATE)

            payload = json.dumps(asdict(tlm)).encode()
            await self.nc.publish(subject, payload)
            logger.debug("Published to %s: %d bytes", subject, len(payload))
        else:
            # Standalone mode - just log
            if packet_type == 0x01:
                logger.info(
                    "MDM state: uptime=%ds ticks=%d mode=0x%02X flags=0x%02X",
                    tlm.uptime_seconds, tlm.tick_count,
                    tlm.mode, tlm.status_flags,
                )

    async def run(self):
        """Run the bridge."""
        tasks = [
            asyncio.create_task(self.read_mdm_output()),
        ]

        if self.nc:
            # Keep NATS connection alive
            async def nats_keepalive():
                while self.running:
                    await asyncio.sleep(1)
            tasks.append(asyncio.create_task(nats_keepalive()))

        # Wait for the MDM process to finish or be killed
        await self.mdm.wait()
        self.running = False
        logger.info("MDM process exited with code %d", self.mdm.returncode)


async def main():
    logger.info("=" * 60)
    logger.info("MDM 1553-to-NATS Bridge v1.0")
    logger.info("NATS URL: %s", NATS_URL)
    logger.info("=" * 60)

    # The MDM process is expected to be piped to us:
    # ./mdm 1 0x12 | python3 bridge.py
    # If stdin is a pipe from the MDM process, read from it.
    # Otherwise, spawn the MDM process.

    if not sys.stdin.isatty():
        # We are the receiving end of a pipe from mdm
        # STDIN is the MDM's STDOUT
        # STDOUT is not used by the MDM in this mode
        logger.info("Running in pipe mode (stdin = MDM stdout)")

        # Create a mock process wrapper around stdin/stdout
        class PipeProcess:
            class StdoutWrapper:
                async def readexactly(self, n):
                    data = sys.stdin.buffer.read(n)
                    if len(data) < n:
                        raise asyncio.IncompleteReadError(data, n)
                    return data

            def __init__(self):
                self.stdout = self.StdoutWrapper()
                self.returncode = 0

            async def wait(self):
                # Wait until stdin closes
                while True:
                    data = sys.stdin.buffer.read(1024)
                    if not data:
                        break
                return 0

        mdm_proc = PipeProcess()
    else:
        # Spawn the MDM process
        mdm_binary = os.environ.get("MDM_BINARY", "./mdm")
        mdm_id = int(os.environ.get("MDM_ID", "1"))
        bus_addr = int(os.environ.get("MDM_BUS_ADDR", "0x12"), 0)

        logger.info("Spawning MDM: %s %d 0x%02X", mdm_binary, mdm_id, bus_addr)

        mdm_proc = await asyncio.create_subprocess_exec(
            mdm_binary, str(mdm_id), hex(bus_addr),
            stdout=asyncio.subprocess.PIPE,
            stdin=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        # Log MDM stderr in background
        async def log_stderr():
            while True:
                line = await mdm_proc.stderr.readline()
                if not line:
                    break
                sys.stderr.write(line.decode())
                sys.stderr.flush()

        asyncio.create_task(log_stderr())

    bridge = MDMBridge(mdm_proc)
    await bridge.connect_nats()
    await bridge.run()


if __name__ == "__main__":
    asyncio.run(main())
