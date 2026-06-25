import logging
from dataclasses import dataclass, field

logger = logging.getLogger(__name__)


@dataclass
class LimitRule:
    subsystem: str
    param: str
    min_val: float
    max_val: float
    severity: str = "warning"


@dataclass
class LimitEvent:
    subsystem: str
    param: str
    value: float
    limit_min: float
    limit_max: float
    severity: str
    message: str = ""


class LimitChecker:
    def __init__(self) -> None:
        self.limits: dict[str, list[LimitRule]] = {}

    def add_limit(
        self,
        subsystem: str,
        param: str,
        min_val: float,
        max_val: float,
        severity: str = "warning",
    ) -> None:
        if subsystem not in self.limits:
            self.limits[subsystem] = []
        self.limits[subsystem].append(
            LimitRule(subsystem, param, min_val, max_val, severity)
        )
        logger.info(
            "Limit added: %s.%s [%.2f, %.2f] (%s)",
            subsystem,
            param,
            min_val,
            max_val,
            severity,
        )

    def check(self, subsystem: str, params: dict) -> list[LimitEvent]:
        events: list[LimitEvent] = []
        rules = self.limits.get(subsystem, [])
        for rule in rules:
            value = params.get(rule.param)
            if value is None:
                continue
            if value < rule.min_val or value > rule.max_val:
                side = "low" if value < rule.min_val else "high"
                msg = (
                    f"{subsystem}.{rule.param}={value:.3f} "
                    f"({side}, limit: [{rule.min_val:.3f}, {rule.max_val:.3f}])"
                )
                events.append(
                    LimitEvent(
                        subsystem=subsystem,
                        param=rule.param,
                        value=value,
                        limit_min=rule.min_val,
                        limit_max=rule.max_val,
                        severity=rule.severity,
                        message=msg,
                    )
                )
        return events
