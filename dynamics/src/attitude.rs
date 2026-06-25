use nalgebra::{Matrix3, Vector3};
use serde::{Deserialize, Serialize};

/// Unit quaternion newtype over `[w, x, y, z]`.
#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct Quaternion(pub [f64; 4]);

impl Quaternion {
    pub fn new(w: f64, x: f64, y: f64, z: f64) -> Self {
        let norm = (w * w + x * x + y * y + z * z).sqrt();
        Self([w / norm, x / norm, y / norm, z / norm])
    }

    pub fn identity() -> Self {
        Self([1.0, 0.0, 0.0, 0.0])
    }

    pub fn multiply(&self, other: &Quaternion) -> Quaternion {
        let [w1, x1, y1, z1] = self.0;
        let [w2, x2, y2, z2] = other.0;
        Quaternion([
            w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
            w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        ])
    }

    pub fn conjugate(&self) -> Quaternion {
        Quaternion([self.0[0], -self.0[1], -self.0[2], -self.0[3]])
    }

    pub fn rotate(&self, vector: &Vector3<f64>) -> Vector3<f64> {
        let p = Quaternion([0.0, vector.x, vector.y, vector.z]);
        let r = self.multiply(&p).multiply(&self.conjugate());
        Vector3::new(r.0[1], r.0[2], r.0[3])
    }
}

/// Attitude state with quaternion orientation and body angular rates (rad/s).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AttitudeState {
    pub quat: Quaternion,
    pub angular_rate: Vector3<f64>,
}

/// Integrate attitude dynamics forward by dt seconds using Euler's equations.
pub fn integrate_attitude(
    state: &AttitudeState,
    torque: Vector3<f64>,
    inertia: Vector3<f64>,
    dt: f64,
) -> AttitudeState {
    let wx = state.angular_rate.x;
    let wy = state.angular_rate.y;
    let wz = state.angular_rate.z;
    let ix = inertia.x;
    let iy = inertia.y;
    let iz = inertia.z;

    let wx_dot = (torque.x - (iz - iy) * wy * wz) / ix;
    let wy_dot = (torque.y - (ix - iz) * wx * wz) / iy;
    let wz_dot = (torque.z - (iy - ix) * wx * wy) / iz;

    let new_rate = Vector3::new(
        wx + wx_dot * dt,
        wy + wy_dot * dt,
        wz + wz_dot * dt,
    );

    let avg_rate = (state.angular_rate + new_rate) * 0.5;
    let [w, x, y, z] = state.quat.0;

    let qd0 = -0.5 * (x * avg_rate.x + y * avg_rate.y + z * avg_rate.z);
    let qd1 = 0.5 * (w * avg_rate.x + y * avg_rate.z - z * avg_rate.y);
    let qd2 = 0.5 * (w * avg_rate.y + z * avg_rate.x - x * avg_rate.z);
    let qd3 = 0.5 * (w * avg_rate.z + x * avg_rate.y - y * avg_rate.x);

    let mut nq = [
        w + qd0 * dt,
        x + qd1 * dt,
        y + qd2 * dt,
        z + qd3 * dt,
    ];

    let norm = (nq[0] * nq[0] + nq[1] * nq[1] + nq[2] * nq[2] + nq[3] * nq[3]).sqrt();
    nq[0] /= norm;
    nq[1] /= norm;
    nq[2] /= norm;
    nq[3] /= norm;

    AttitudeState {
        quat: Quaternion(nq),
        angular_rate: new_rate,
    }
}

/// Compute gravity-gradient torque in the ECI frame.
pub fn torque_gravity_gradient(pos_eci: Vector3<f64>, inertia: Matrix3<f64>) -> Vector3<f64> {
    let mu = crate::environment::earth_gravitational_constant();
    let r = pos_eci.norm();
    if r < 1.0 {
        return Vector3::zeros();
    }
    let r_hat = pos_eci / r;
    let ir = inertia * r_hat;
    3.0 * mu / r.powi(3) * r_hat.cross(&ir)
}

/// Compute aerodynamic torque from atmospheric drag.
pub fn torque_aerodynamic(
    vel_eci: Vector3<f64>,
    density: f64,
    cp_offset: Vector3<f64>,
    area: f64,
    cd: f64,
) -> Vector3<f64> {
    let v = vel_eci.norm();
    if v < 1e-6 || density < 1e-15 {
        return Vector3::zeros();
    }
    let v_hat = vel_eci / v;
    let force_mag = 0.5 * density * v * v * cd * area;
    cp_offset.cross(&(-v_hat * force_mag))
}

/// Compute magnetic torque from residual dipole moment.
pub fn torque_magnetic(
    residual_magnetic: Vector3<f64>,
    earth_field_eci: Vector3<f64>,
) -> Vector3<f64> {
    residual_magnetic.cross(&earth_field_eci)
}
