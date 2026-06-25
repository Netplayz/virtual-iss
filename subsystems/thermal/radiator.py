import logging
import math

logger = logging.getLogger(__name__)


class Radiator:
    def __init__(
        self,
        area_m2: float = 100.0,
        emissivity: float = 0.85,
        angle_deg: float = 0.0,
    ) -> None:
        self.area_m2 = area_m2
        self.emissivity = emissivity
        self.angle_deg = angle_deg
        self._stefan_boltzmann: float = 5.670374419e-8

    def reject_heat(
        self, coolant_temp_c: float, environment_temp_c: float
    ) -> float:
        effective_area = self.get_effective_area(self.angle_deg)
        t_coolant_k = coolant_temp_c + 273.15
        t_env_k = environment_temp_c + 273.15
        heat = (
            self.emissivity
            * self._stefan_boltzmann
            * effective_area
            * (t_coolant_k**4 - t_env_k**4)
        )
        heat = max(0.0, heat)
        logger.debug(
            "Radiator rejecting %.1f W (area=%.1f m2, T=%.1f K, T_env=%.1f K)",
            heat,
            effective_area,
            t_coolant_k,
            t_env_k,
        )
        return heat

    def set_angle(self, deg: float) -> None:
        self.angle_deg = max(-90.0, min(90.0, deg))
        logger.info("Radiator angle set to %.1f deg", self.angle_deg)

    @staticmethod
    def get_effective_area(angle_deg: float) -> float:
        return 100.0 * abs(math.cos(math.radians(angle_deg)))
