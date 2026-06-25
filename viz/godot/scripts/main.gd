extends Node3D

var telemetry_bridge: Node
var iss_model: Node3D
var hud: CanvasLayer
var camera: Camera3D

func _ready():
	telemetry_bridge = preload("res://scripts/telemetry_bridge.gd").new()
	add_child(telemetry_bridge)
	telemetry_bridge.connect("telemetry_updated", _on_telemetry_updated)
	telemetry_bridge.connect("alarm_triggered", _on_alarm)

	iss_model = $ISS
	hud = $HUD
	camera = $Camera3D

	telemetry_bridge.start()

func _on_telemetry_updated(data: Dictionary):
	var subsystem = data.get("subsystem", "")
	if subsystem == "dynamics":
		_update_dynamics(data)
	elif subsystem == "eps":
		_update_eps(data)
	elif subsystem == "eclss":
		_update_eclss(data)

func _update_dynamics(data: Dictionary):
	if iss_model and iss_model.has_method("update_state"):
		iss_model.update_state(data)

	var pos = data.get("position_eci_m", {})
	var lat = data.get("latitude_deg", 0.0)
	var lon = data.get("longitude_deg", 0.0)
	var alt = data.get("altitude_m", 0.0)
	var beta = data.get("beta_angle_deg", 0.0)

	hud.update_orbit(alt, lat, lon, beta)

func _update_eps(data: Dictionary):
	var solar_gen = data.get("total_power_gen_w", 0.0)
	var battery_soc = data.get("battery_soc_pct", 0.0)
	var load = data.get("total_power_load_w", 0.0)
	var load_shed = data.get("load_shed_active", false)

	hud.update_power(solar_gen, battery_soc, load, load_shed)

func _update_eclss(data: Dictionary):
	var o2 = data.get("o2_partial_pressure_kpa", 0.0)
	var co2 = data.get("co2_partial_pressure_kpa", 0.0)
	var temp = data.get("cabin_temp_c", 0.0)
	var press = data.get("cabin_pressure_kpa", 0.0)

	hud.update_eclss(o2, co2, temp, press)

func _on_alarm(message: String, severity: String):
	hud.trigger_alarm(message, severity)
