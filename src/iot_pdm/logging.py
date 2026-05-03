"""Structured JSON logging via structlog."""
import logging
import sys

import orjson
import structlog
from structlog.contextvars import merge_contextvars
from structlog.processors import (
    JSONRenderer,
    StackInfoRenderer,
    TimeStamper,
    add_log_level,
)

from iot_pdm.settings import get_settings


def configure_logging() -> None:
    s = get_settings()
    level = getattr(logging, s.log_level.upper(), logging.INFO)

    logging.basicConfig(
        format="%(message)s",
        stream=sys.stdout,
        level=level,
    )
    logging.getLogger("asyncio").setLevel(logging.WARNING)
    logging.getLogger("aiomqtt").setLevel(logging.INFO)

    structlog.configure(
        processors=[
            merge_contextvars,
            add_log_level,
            TimeStamper(fmt="iso", utc=True),
            StackInfoRenderer(),
            structlog.processors.format_exc_info,
            JSONRenderer(serializer=lambda o, **_: orjson.dumps(o).decode()),
        ],
        wrapper_class=structlog.make_filtering_bound_logger(level),
        context_class=dict,
        logger_factory=structlog.PrintLoggerFactory(),
        cache_logger_on_first_use=True,
    )


def get_logger(name: str = "iot_pdm"):
    return structlog.get_logger(name)
