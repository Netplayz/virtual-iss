use nalgebra::{Unit, UnitQuaternion, Vector3};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub enum ControlMode {
    Idle,
    SunPointing,
    EarthPointing,
    InertialHold,
    MomentumUnload,
    Slew,
}

impl ControlMode {
    pub fn as_str(&self) -> &'static str {
        match self {
            ControlMode::Idle => "idle",
            ControlMode::SunPointing => "sun_pointing",
            ControlMode::EarthPointing => "earth_pointing",
            ControlMode::InertialHold => "inertial_hold",
            ControlMode::MomentumUnload => "momentum_unload",
            ControlMode::Slew => "slew",
        }
    }

    pub fn from_str_option(s: &str) -> Option<Self> {
        match s {
            "idle" => Some(ControlMode::Idle),
            "sun_pointing" => Some(ControlMode::SunPointing),
            "earth_pointing" => Some(ControlMode::EarthPointing),
            "inertial_hold" => Some(ControlMode::InertialHold),
            "momentum_unload" => Some(ControlMode::MomentumUnload),
            "slew" => Some(ControlMode::Slew),
            _ => None,
        }
    }
}

#[derive(Debug, Clone)]
pub struct AttitudeController {
    pub kp: f64,
    pub kd: f64,
    pub ki: f64,
    pub rate_limit: f64,
    pub torque_limit: f64,
    pub integral_windup_limit: f64,
    integral: Vector3<f64>,
}

impl AttitudeController {
    pub fn new() -> Self {
        AttitudeController {
            kp: 0.5,
            kd: 2.0,
            ki: 0.01,
            rate_limit: 0.05,
            torque_limit: 0.2,
            integral_windup_limit: 0.1,
            integral: Vector3::zeros(),
        }
    }

    pub fn compute_control_torque(
        &mut self,
        target_quat: &[f64; 4],
        current_est: &[f64; 4],
        rate: Vector3<f64>,
        dt: f64,
    ) -> Vector3<f64> {
        let q_target = crate::array_to_quat(target_quat);
        let q_current = crate::array_to_quat(current_est);

        let q_err = q_target.inverse() * q_current;

        let q_ev = q_err.vector();
        let q_e0 = q_err.scalar();
        let sign = if q_e0 >= 0.0 { 1.0 } else { -1.0 };

        let rate_feedback = -self.kd * rate;

        let proportional = -self.kp * sign * q_ev;

        self.integral += sign * q_ev * dt * self.ki;
        let int_norm = self.integral.norm();
        if int_norm > self.integral_windup_limit {
            self.integral *= self.integral_windup_limit / int_norm;
        }

        let mut torque = proportional + rate_feedback + self.integral;

        let torq_norm = torque.norm();
        if torq_norm > self.torque_limit {
            torque *= self.torque_limit / torq_norm;
        }

        torque
    }

    pub fn reset_integral(&mut self) {
        self.integral = Vector3::zeros();
    }
}

pub fn compute_sun_pointing_target(sun_vector_body: Vector3<f64>) -> [f64; 4] {
    let body_x = Vector3::x();
    let sun_dir = sun_vector_body.normalize();
    let axis = body_x.cross(&sun_dir);
    let dot = body_x.dot(&sun_dir).clamp(-1.0, 1.0);
    let angle = dot.acos();

    if axis.norm() < 1e-12 {
        if angle.abs() < 1e-12 {
            return [1.0, 0.0, 0.0, 0.0];
        }
        let perp = if body_x.x.abs() < 0.9 {
            Vector3::x().cross(&body_x)
        } else {
            Vector3::y().cross(&body_x)
        };
        let q = UnitQuaternion::from_axis_angle(&Unit::new_normalize(perp), angle);
        return crate::quat_to_array(&q);
    }

    let q = UnitQuaternion::from_axis_angle(&Unit::new_normalize(axis), angle);
    crate::quat_to_array(&q)
}

pub fn compute_nadir_pointing_target(position_eci: Vector3<f64>) -> [f64; 4] {
    let body_minus_z = -Vector3::z();
    let pos_norm = position_eci.norm();
    if pos_norm < 1e-12 {
        return [1.0, 0.0, 0.0, 0.0];
    }
    let nadir = -position_eci / pos_norm;

    let axis = body_minus_z.cross(&nadir);
    let dot = body_minus_z.dot(&nadir).clamp(-1.0, 1.0);
    let angle = dot.acos();

    if axis.norm() < 1e-12 {
        if angle.abs() < 1e-12 {
            return [1.0, 0.0, 0.0, 0.0];
        }
        let perp = if body_minus_z.x.abs() < 0.9 {
            Vector3::x().cross(&body_minus_z)
        } else {
            Vector3::y().cross(&body_minus_z)
        };
        let q = UnitQuaternion::from_axis_angle(&Unit::new_normalize(perp), angle);
        return crate::quat_to_array(&q);
    }

    let q = UnitQuaternion::from_axis_angle(&Unit::new_normalize(axis), angle);
    crate::quat_to_array(&q)
}
