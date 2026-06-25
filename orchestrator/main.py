#!/usr/bin/env python3
"""Orchestrator — simulation time keeper and scenario controller."""
import asyncio
import logging

import nats

from .orchestrator import Orchestrator

logger = logging.getLogger("orchestrator")


async def main():
    nc = await nats.connect(os.environ.get("NATS_URL", "nats://localhost:4222"))
    orchestrator = Orchestrator(nc)
    await orchestrator.run()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(main())
