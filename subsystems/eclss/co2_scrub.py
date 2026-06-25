import logging

logger = logging.getLogger(__name__)

CDRA_BED_COUNT = 2
CDRA_REGENERATION_INTERVAL_S = 144 * 60


class CDRAScrubber:
    def __init__(self) -> None:
        self._active_bed: int = 0
        self._time_since_regeneration: float = 0.0
        self._max_rate_kg_s: float = 4.0 / 86400.0

    def scrub_co2(
        self,
        co2_partial_kpa: float,
        cabin_pressure_kpa: float,
        dt_s: float,
    ) -> float:
        co2_fraction = co2_partial_kpa / cabin_pressure_kpa
        removed = self._max_rate_kg_s * max(0.0, co2_fraction * 100) * dt_s
        self._time_since_regeneration += dt_s
        logger.debug(
            "Bed %d scrubbed %.6f kg CO2", self._active_bed, removed
        )
        return removed

    def regeneration_cycle(self) -> None:
        if self._time_since_regeneration >= CDRA_REGENERATION_INTERVAL_S:
            self._active_bed = (self._active_bed + 1) % CDRA_BED_COUNT
            self._time_since_regeneration = 0.0
            logger.info(
                "Switched to CDRA bed %d for regeneration",
                self._active_bed,
            )
