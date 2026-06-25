import logging

logger = logging.getLogger(__name__)


class ElectrolysisO2Gen:
    def __init__(self) -> None:
        self._power_rating: float = 2500.0
        self._efficiency: float = 0.70
        self._energy_per_kg_j: float = 12.0 * 3.6e6

    def produce_o2(
        self, power_w: float, o2_partial_kpa: float, dt_s: float
    ) -> float:
        actual_power = min(power_w, self._power_rating)
        energy_j = actual_power * dt_s * self._efficiency
        o2_mass = energy_j / self._energy_per_kg_j
        logger.debug(
            "Produced %.4f kg O2 from %.1f W over %.1f s",
            o2_mass,
            actual_power,
            dt_s,
        )
        return o2_mass

    def get_power_demand(self) -> float:
        return self._power_rating
