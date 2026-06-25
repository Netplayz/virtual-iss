import asyncio
import logging
import os

from nats.aio.client import Client as NATS

from .simulator import ECLSSSimulator

logger = logging.getLogger(__name__)


async def main() -> None:
    nats_url = os.environ.get("NATS_URL", "nats://localhost:4222")

    nc = NATS()
    await nc.connect(nats_url)
    logger.info("Connected to NATS at %s", nats_url)

    sim = ECLSSSimulator(nc)
    try:
        await sim.run()
    except asyncio.CancelledError:
        logger.info("ECLSS simulator shutting down")
    finally:
        await nc.drain()
        await nc.close()


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    asyncio.run(main())
