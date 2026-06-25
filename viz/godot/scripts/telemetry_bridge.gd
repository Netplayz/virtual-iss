extends Node

signal telemetry_updated(data: Dictionary)
signal alarm_triggered(message: String, severity: String)

var _http: HTTPRequest
var _last_states: Dictionary = {}

const MCP_URL: String = "http://localhost:8331"
const POLL_INTERVAL_MS: int = 100

func start() -> void:
	_http = HTTPRequest.new()
	add_child(_http)
	_http.connect("request_completed", _on_data_received)
	_fetch()

func _fetch() -> void:
	var err = _http.request(MCP_URL + "/api/v1/stream")
	if err != OK:
		push_warning("TelemetryBridge: HTTP request failed with code ", err)
	await get_tree().create_timer(POLL_INTERVAL_MS / 1000.0).timeout
	_fetch()

func _on_data_received(result: int, response_code: int, headers: PackedStringArray, body: PackedByteArray) -> void:
	if result != HTTPRequest.RESULT_SUCCESS:
		push_warning("TelemetryBridge: request failed, retrying...")
		return

	var text = body.get_string_from_utf8()
	var lines = text.split("\n")
	for line in lines:
		line = line.strip_edges()
		if line.begins_with("data: "):
			var json_str = line.substr(6)
			var json = JSON.new()
			var err = json.parse(json_str)
			if err == OK:
				var data = json.data as Dictionary
				if data.has("alarm"):
					var alarm = data["alarm"] as Dictionary
					alarm_triggered.emit(alarm.get("message", ""), alarm.get("severity", "INFO"))
				else:
					var sub = data.get("subsystem", "")
					_last_states[sub] = data
					telemetry_updated.emit(data)

func get_latest(subsystem: String) -> Dictionary:
	return _last_states.get(subsystem, {})
