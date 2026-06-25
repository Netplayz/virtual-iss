extends Camera3D

var _drag_origin: Vector2
var _rot_x: float = 0.0
var _rot_y: float = 0.0
var _zoom: float = 100.0

const ZOOM_MIN: float = 5.0
const ZOOM_MAX: float = 500.0
const ORBIT_SPEED: float = 0.005
const ZOOM_SPEED: float = 10.0
const PAN_SPEED: float = 0.01

var _pan_offset: Vector3 = Vector3.ZERO
var _is_panning: bool = false
var _target: Node3D

func _ready():
	_target = get_parent().get_node("ISS") if get_parent().has_node("ISS") else null
	if not _target:
		_target = get_node("/root/Main/ISS") if has_node("/root/Main/ISS") else null

func _input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_zoom = max(ZOOM_MIN, _zoom - ZOOM_SPEED)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_zoom = min(ZOOM_MAX, _zoom + ZOOM_SPEED)
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			_is_panning = event.pressed
			if _is_panning:
				_drag_origin = event.position

	elif event is InputEventMouseMotion:
		if event.button_mask == MOUSE_BUTTON_MASK_LEFT:
			_rot_x += event.relative.x * ORBIT_SPEED
			_rot_y += event.relative.y * ORBIT_SPEED
			_rot_y = clamp(_rot_y, -1.5, 1.5)
		elif _is_panning:
			var delta = (event.position - _drag_origin) * PAN_SPEED
			_pan_offset += Vector3(delta.x, -delta.y, 0.0)
			_drag_origin = event.position

func _process(delta: float) -> void:
	if not _target:
		return

	var base = _target.global_transform.origin
	var offset = Vector3(
		sin(_rot_x) * cos(_rot_y) * _zoom,
		sin(_rot_y) * _zoom,
		cos(_rot_x) * cos(_rot_y) * _zoom
	)
	global_transform.origin = base + offset + _pan_offset
	look_at(base + _pan_offset, Vector3.UP)

func set_preset(preset: String) -> void:
	match preset:
		"cupola":
			_rot_x = 0.0
			_rot_y = 0.2
			_zoom = 15.0
			_pan_offset = Vector3.ZERO
		"full":
			_rot_x = 0.8
			_rot_y = -0.3
			_zoom = 80.0
			_pan_offset = Vector3.ZERO
		"solar_array":
			_rot_x = 0.0
			_rot_y = 0.0
			_zoom = 30.0
			_pan_offset = Vector3(-20, 0, 0)
