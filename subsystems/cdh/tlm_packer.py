import hashlib
import struct
import time
from typing import Any


def pack_telemetry(subsystem: str, data: dict) -> dict:
    now = time.time()
    payload = f"{subsystem}:{data}".encode()
    crc = hashlib.sha256(payload).hexdigest()[:8]
    packet = {
        "subsystem": subsystem,
        "data": data,
        "timestamp": now,
        "sequence": int(now * 1000) & 0xFFFFFFFF,
        "crc": crc,
    }
    return packet


def format_for_storage(packet: dict) -> bytes:
    ts_bytes = struct.pack(">d", packet.get("timestamp", 0.0))
    seq_bytes = struct.pack(">I", packet.get("sequence", 0))
    crc_bytes = packet.get("crc", "00000000").encode("ascii")
    subsystem_bytes = packet.get("subsystem", "unk").encode("ascii").ljust(8, b"\x00")
    data_str = str(packet.get("data", {})).encode("ascii")
    return b"".join([ts_bytes, seq_bytes, crc_bytes, subsystem_bytes, data_str])
