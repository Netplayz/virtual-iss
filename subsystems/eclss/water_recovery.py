import logging

logger = logging.getLogger(__name__)


class WaterRecovery:
    def __init__(self) -> None:
        self._urine_recovery_pct: float = 0.85
        self._humidity_recovery_pct: float = 0.95
        self._water_stored_l: float = 0.0
        self._max_production_l_day: float = 6.8

    def process_urine(self, volume_l: float) -> float:
        recovered = volume_l * self._urine_recovery_pct
        max_l = self._max_production_l_day / 86400.0
        recovered = min(recovered, max_l)
        self._water_stored_l += recovered
        logger.debug("Recovered %.4f L from urine", recovered)
        return recovered

    def process_humidity(
        self, humidity_pct: float, cabin_temp_c: float
    ) -> float:
        vapour_pressure = self._saturation_pressure(cabin_temp_c) * (
            humidity_pct / 100.0
        )
        recovered = vapour_pressure * 0.0005 * self._humidity_recovery_pct
        self._water_stored_l += recovered
        logger.debug("Recovered %.4f L from humidity", recovered)
        return recovered

    def get_water_stored(self) -> float:
        return self._water_stored_l

    @staticmethod
    def _saturation_pressure(temp_c: float) -> float:
        return 0.61078 * (2.71828 ** ((17.27 * temp_c) / (temp_c + 237.3)))
