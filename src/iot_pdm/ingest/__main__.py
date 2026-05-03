"""Entry point: `python -m iot_pdm.ingest`."""
import asyncio

from iot_pdm.ingest.worker import run
from iot_pdm.logging import configure_logging, get_logger


def main() -> None:
    configure_logging()
    log = get_logger()
    log.info("worker.boot")
    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        log.info("worker.shutdown.sigint")


if __name__ == "__main__":
    main()
