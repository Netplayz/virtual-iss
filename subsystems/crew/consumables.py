import logging

logger = logging.getLogger(__name__)

O2_PER_PERSON_KG_S = 0.84 / 86400.0
CO2_PER_PERSON_KG_S = 1.0 / 86400.0
WATER_PER_PERSON_L_S = 2.8 / 86400.0
FOOD_PER_PERSON_KG_S = 1.8 / 86400.0


class ConsumablesTracker:
    def __init__(
        self,
        food_kg: float = 120.0,
        water_l: float = 200.0,
        o2_kg: float = 50.0,
    ) -> None:
        self._food_kg = food_kg
        self._water_l = water_l
        self._o2_kg = o2_kg

    def consume(self, crew_count: int, dt_s: float) -> None:
        self._food_kg -= crew_count * FOOD_PER_PERSON_KG_S * dt_s
        self._water_l -= crew_count * WATER_PER_PERSON_L_S * dt_s
        self._o2_kg -= crew_count * O2_PER_PERSON_KG_S * dt_s

        self._food_kg = max(0.0, self._food_kg)
        self._water_l = max(0.0, self._water_l)
        self._o2_kg = max(0.0, self._o2_kg)

        logger.debug(
            "Consumed resources for %d crew over %.1f s: food=%.3f kg, water=%.3f L, O2=%.3f kg",
            crew_count,
            dt_s,
            self._food_kg,
            self._water_l,
            self._o2_kg,
        )

    def get_remaining(self) -> dict:
        return {
            "food_kg": self._food_kg,
            "water_l": self._water_l,
            "o2_kg": self._o2_kg,
        }
