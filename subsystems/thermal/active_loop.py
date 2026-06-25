import logging

logger = logging.getLogger(__name__)


class ActiveThermalLoop:
    def __init__(self) -> None:
        self.loop_a_temp_in: float = 3.0
        self.loop_a_temp_out: float = 6.0
        self.loop_b_temp_in: float = 2.0
        self.loop_b_temp_out: float = 5.0
        self._flow_rate_lpm: float = 200.0
        self._coolant_mass_kg: float = 50.0
        self._specific_heat_j_kgk: float = 4184.0

    def update(
        self,
        heat_load_w: float,
        radiator_temp_c: float,
        dt_s: float,
    ) -> tuple[float, float, float]:
        dtemp = heat_load_w * dt_s / (self._coolant_mass_kg * self._specific_heat_j_kgk)
        self.loop_a_temp_in += dtemp
        self.loop_a_temp_out = self.loop_a_temp_in + 3.0

        rejection = max(0.0, self.loop_a_temp_out - radiator_temp_c)
        self.loop_a_temp_out -= rejection * 0.01 * dt_s
        self.loop_a_temp_in = (
            self.loop_a_temp_out * 0.7 + self.loop_a_temp_in * 0.3
        )
        self._flow_rate_lpm = 200.0 + heat_load_w / 500.0

        logger.debug(
            "Loop A: in=%.1f C, out=%.1f C, flow=%.1f LPM",
            self.loop_a_temp_in,
            self.loop_a_temp_out,
            self._flow_rate_lpm,
        )
        return (self.loop_a_temp_in, self.loop_a_temp_out, self._flow_rate_lpm)

    def get_heat_rejection(self) -> float:
        delta_t = self.loop_a_temp_out - radiator_avg_temp(self.loop_a_temp_in, self.loop_a_temp_out)
        return (
            self._flow_rate_lpm / 60.0
            * self._coolant_mass_kg
            * self._specific_heat_j_kgk
            * delta_t
        )


def radiator_avg_temp(t_in: float, t_out: float) -> float:
    return (t_in + t_out) / 2.0
