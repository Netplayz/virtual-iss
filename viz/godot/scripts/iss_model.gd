extends Node3D

var target_position: Vector3
var target_quat: Quaternion
var target_solar_angle: float = 0.0
var target_radiator_angle: float = 0.0

var current_solar_angle: float = 0.0
var current_radiator_angle: float = 0.0

const LERP_SPEED: float = 5.0
const SCALE: float = 0.001

@onready var solar_left: Node3D = $SolarArray_Left
@onready var solar_right: Node3D = $SolarArray_Right
@onready var radiator_left: Node3D = $Radiator_Left
@onready var radiator_right: Node3D = $Radiator_Right

func update_state(data: Dictionary) -> void:
	var pos = data.get("position_eci_m", {})
	var x = pos.get("x", 0.0) * SCALE
	var y = pos.get("y", 0.0) * SCALE
	var z = pos.get("z", 0.0) * SCALE
	target_position = Vector3(x, y, z)

	var quat_arr = data.get("quaternion", [1.0, 0.0, 0.0, 0.0])
	target_quat = Quaternion(quat_arr[0], quat_arr[1], quat_arr[2], quat_arr[3])

	target_solar_angle = data.get("beta_angle_deg", 0.0)

	var thermal = data.get("thermal", {})
	target_radiator_angle = thermal.get("radiator_angle_deg", current_radiator_angle)

func _process(delta: float) -> void:
	position = position.lerp(target_position, delta * LERP_SPEED)

	var current_rot = Quaternion(transform.basis.get_rotation_quaternion())
	var new_rot = current_rot.slerp(target_quat, delta * LERP_SPEED)
	transform.basis = Basis(new_rot)

	current_solar_angle = lerp(current_solar_angle, target_solar_angle, delta * LERP_SPEED)
	current_radiator_angle = lerp(current_radiator_angle, target_radiator_angle, delta * LERP_SPEED)

	if solar_left:
		solar_left.rotation.x = deg_to_rad(current_solar_angle)
	if solar_right:
		solar_right.rotation.x = deg_to_rad(current_solar_angle)

	if radiator_left:
		radiator_left.rotation.x = deg_to_rad(current_radiator_angle)
	if radiator_right:
		radiator_right.rotation.x = deg_to_rad(current_radiator_angle)
