pub mod sensors;
pub mod estimator;
pub mod control;
pub mod steering;
pub mod controller;

use nalgebra::{Quaternion, UnitQuaternion, Vector3};

pub fn quat_to_array(q: &UnitQuaternion<f64>) -> [f64; 4] {
    let inner = q.quaternion();
    [inner.w, inner.i, inner.j, inner.k]
}

pub fn array_to_quat(arr: &[f64; 4]) -> UnitQuaternion<f64> {
    UnitQuaternion::new_normalize(Quaternion::new(arr[0], arr[1], arr[2], arr[3]))
}

pub fn vec3_to_array(v: &Vector3<f64>) -> [f64; 3] {
    [v.x, v.y, v.z]
}

pub fn array_to_vec3(arr: &[f64; 3]) -> Vector3<f64> {
    Vector3::new(arr[0], arr[1], arr[2])
}

pub fn vec3_from_msg(x: f64, y: f64, z: f64) -> Vector3<f64> {
    Vector3::new(x, y, z)
}
