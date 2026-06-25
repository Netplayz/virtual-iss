import logging

logger = logging.getLogger(__name__)


class HeaterZone:
    def __init__(
        self,
        name: str,
        setpoint_c: float,
        power_rating_w: float,
        deadband_c: float = 1.0,
    ) -> None:
        self.name = name
        self.setpoint_c = setpoint_c
        self.power_rating_w = power_rating_w
        self.deadband_c = deadband_c


class HeaterSystem:
    def __init__(self) -> None:
        self.zones: dict[str, HeaterZone] = {
            "cabin": HeaterZone("cabin", 22.0, 1500.0),
            "node1": HeaterZone("node1", 18.0, 800.0),
            "node2": HeaterZone("node2", 18.0, 800.0),
            "lab": HeaterZone("lab", 20.0, 1000.0),
        }

    def add_zone(self, name: str, setpoint_c: float, power_w: float) -> None:
        self.zones[name] = HeaterZone(name, setpoint_c, power_w)

    def regulate(self, temp_c: float, setpoint_c: float, dt_s: float) -> float:
        error = setpoint_c - temp_c
        if error > self.zones["cabin"].deadband_c:
            power = self.zones["cabin"].power_rating_w * min(
                1.0, error / 10.0
            )
        elif error < -self.zones["cabin"].deadband_c:
            power = 0.0
        else:
            power = self.zones["cabin"].power_rating_w * 0.1

        logger.debug(
            "Heater regulating: T=%.1f set=%.1f power=%.1f W",
            temp_c,
            setpoint_c,
            power,
        )
        return max(0.0, power)
