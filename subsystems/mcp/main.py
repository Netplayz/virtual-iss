#!/usr/bin/env python3
"""MCP Server for Virtual ISS — Model Context Protocol interface."""
import asyncio
import logging
import os

import nats

from server import MCPServer

logger = logging.getLogger("mcp")


async def main():
    nc = await nats.connect(os.environ.get("NATS_URL", "nats://localhost:4222"))
    logger.info("connected to NATS")
    server = MCPServer(nc)
    await server.run()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(main())
