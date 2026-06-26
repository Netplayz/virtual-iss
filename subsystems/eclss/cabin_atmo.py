import logging

logger = logging.getLogger(__name__)


class CabinAtmosphere:
    def __init__(
        self,
        volume_m3: float = 930.0,
        pressure_kpa: float = 101.3,
        temp_c: float = 22.0,
        humidity_pct: float = 50.0,
        o2_partial_kpa: float = 21.2,
        co2_partial_kpa: float = 0.04,
    ) -> None:
        self.volume_m3 = volume_m3
        self.pressure_kpa = pressure_kpa
        self.temp_c = temp_c
        self.humidity_pct = humidity_pct
        self.o2_partial_kpa = o2_partial_kpa
        self.co2_partial_kpa = co2_partial_kpa

        self._o2_per_person_kg_s: float = 0.84 / 86400.0
        self._co2_per_person_kg_s: float = 1.0 / 86400.0

    def update(
        self,
        dt_s: float,
        crew_count: int,
        o2_produced: float,
        co2_removed: float,
        leak_rate: float = 0.0,
    ) -> None:
        o2_consumed = crew_count * self._o2_per_person_kg_s * dt_s
        co2_produced = crew_count * self._co2_per_person_kg_s * dt_s

        net_o2 = o2_produced - o2_consumed
        net_co2 = co2_produced - co2_removed

        air_mass_kg = (self.pressure_kpa * self.volume_m3) / (0.287 * (self.temp_c + 273.15))
        o2_mass = self.o2_partial_kpa / self.pressure_kpa * air_mass_kg
        co2_mass = self.co2_partial_kpa / self.pressure_kpa * air_mass_kg

        o2_mass += net_o2
        co2_mass += net_co2

        if leak_rate > 0:
            self.pressure_kpa -= leak_rate * dt_s

        o2_mass = max(o2_mass, 0.01 * air_mass_kg)
        co2_mass = max(co2_mass, 0.0)

        self.o2_partial_kpa = (o2_mass / air_mass_kg) * self.pressure_kpa
        self.co2_partial_kpa = (co2_mass / air_mass_kg) * self.pressure_kpa
        self.humidity_pct += (55.0 - self.humidity_pct) * 0.001 * dt_s

        logger.debug(
            "Atmo updated: O2=%.2f kPa, CO2=%.2f kPa, T=%.1f C, H=%.1f%%",
            self.o2_partial_kpa,
            self.co2_partial_kpa,
            self.temp_c,
            self.humidity_pct,
        )

    def get_state(self) -> dict:
        return {
            "volume_m3": self.volume_m3,
            "cabin_pressure_kpa": self.pressure_kpa,
            "cabin_temp_c": self.temp_c,
            "humidity_pct": self.humidity_pct,
            "o2_partial_kpa": self.o2_partial_kpa,
            "co2_partial_kpa": self.co2_partial_kpa,
        }
