extends CanvasLayer

@onready var telemetry_label: Label = $TelemetryLabel
@onready var alarm_label: Label = $AlarmLabel

var _alarm_timer: float = 0.0
var _alarm_active: bool = false
var _alarm_flash: bool = false

var _orbit_text: String = ""
var _power_text: String = ""
var _eclss_text: String = ""

func update_orbit(alt_m: float, lat_deg: float, lon_deg: float, beta_deg: float) -> void:
	_orbit_text = "ALT: %.1f km | LAT: %.2f | LON: %.2f | Beta: %.1f°" % [alt_m / 1000.0, lat_deg, lon_deg, beta_deg]
	_refresh()

func update_power(gen_w: float, soc_pct: float, load_w: float, load_shed: bool) -> void:
	var shed_str = " [SHED]" if load_shed else ""
	_power_text = "GEN: %.0f W | BAT: %.1f%% | LOAD: %.0f W%s" % [gen_w, soc_pct, load_w, shed_str]
	_refresh()

func update_eclss(o2_kpa: float, co2_kpa: float, temp_c: float, press_kpa: float) -> void:
	_eclss_text = "O2: %.1f kPa | CO2: %.2f kPa | T: %.1f°C | P: %.1f kPa" % [o2_kpa, co2_kpa, temp_c, press_kpa]
	_refresh()

func _refresh() -> void:
	telemetry_label.text = _orbit_text + "\n" + _power_text + "\n" + _eclss_text

func trigger_alarm(message: String, severity: String) -> void:
	_alarm_active = true
	_alarm_timer = 10.0
	alarm_label.text = "[%s] %s" % [severity, message]
	alarm_label.visible = true

func _process(delta: float) -> void:
	if _alarm_active:
		_alarm_timer -= delta
		_alarm_flash = not _alarm_flash
		alarm_label.modulate = Color(1, 0, 0, 1.0 if _alarm_flash else 0.3)
		if _alarm_timer <= 0.0:
			_alarm_active = false
			alarm_label.visible = false
