use nalgebra::{Matrix3, SMatrix, UnitQuaternion, Unit, Vector3};
use crate::sensors::{GyroOutput, StarTrackerOutput};

type Matrix6 = SMatrix<f64, 6, 6>;
type Matrix3x6 = SMatrix<f64, 3, 6>;

#[derive(Debug, Clone)]
pub struct GnCEstimationState {
    pub quat: UnitQuaternion<f64>,
    pub rate: Vector3<f64>,
    pub gyro_bias: Vector3<f64>,
}

#[derive(Debug, Clone)]
pub struct ExtendedKalmanFilter {
    pub state: GnCEstimationState,
    pub covariance: Matrix6,
    pub gyro_noise_var: f64,
    pub gyro_bias_var: f64,
    pub star_tracker_noise_var: f64,
    last_gyro_rate: Vector3<f64>,
    has_gyro: bool,
}

fn skew_symmetric(v: &Vector3<f64>) -> Matrix3<f64> {
    Matrix3::new(
        0.0, -v.z, v.y,
        v.z, 0.0, -v.x,
        -v.y, v.x, 0.0,
    )
}

impl ExtendedKalmanFilter {
    pub fn new() -> Self {
        let state = GnCEstimationState {
            quat: UnitQuaternion::identity(),
            rate: Vector3::zeros(),
            gyro_bias: Vector3::zeros(),
        };

        let covariance = Matrix6::identity() * 0.1;

        ExtendedKalmanFilter {
            state,
            covariance,
            gyro_noise_var: 1e-8,
            gyro_bias_var: 1e-12,
            star_tracker_noise_var: 1e-6,
            last_gyro_rate: Vector3::zeros(),
            has_gyro: false,
        }
    }

    pub fn update_gyro(&mut self, measurement: &GyroOutput) {
        self.last_gyro_rate = measurement.angular_rate;
        self.has_gyro = true;
    }

    pub fn predict(&mut self, dt: f64) {
        if !self.has_gyro || dt <= 0.0 {
            return;
        }

        let omega = self.last_gyro_rate - self.state.gyro_bias;
        let angle = omega.norm() * dt;

        if angle > 1e-12 {
            let axis = Unit::new_normalize(omega);
            let q_inc = UnitQuaternion::from_axis_angle(&axis, angle);
            self.state.quat = q_inc * self.state.quat;
        }

        let omega_skew = skew_symmetric(&omega);
        let i3 = Matrix3::identity();
        let z3: Matrix3<f64> = Matrix3::zeros();

        let mut phi = Matrix6::identity();
        phi.fixed_view_mut::<3, 3>(0, 0)
            .copy_from(&(i3 - omega_skew * dt));
        phi.fixed_view_mut::<3, 3>(0, 3)
            .copy_from(&(-i3 * dt));

        let q11 = (self.gyro_noise_var * dt + self.gyro_bias_var * dt.powi(3) / 3.0) * i3;
        let q12 = (-self.gyro_bias_var * dt.powi(2) / 2.0) * i3;
        let q22 = (self.gyro_bias_var * dt) * i3;

        let mut qd = Matrix6::zeros();
        qd.fixed_view_mut::<3, 3>(0, 0).copy_from(&q11);
        qd.fixed_view_mut::<3, 3>(0, 3).copy_from(&q12);
        qd.fixed_view_mut::<3, 3>(3, 0).copy_from(&q12);
        qd.fixed_view_mut::<3, 3>(3, 3).copy_from(&q22);

        self.covariance = phi * self.covariance * phi.transpose() + qd;
    }

    pub fn update_star_tracker(&mut self, measurement: &StarTrackerOutput) {
        let q_m = crate::array_to_quat(&measurement.quat_estimate);
        let q_err = q_m * self.state.quat.inverse();
        let delta_theta = 2.0 * q_err.vector();

        let i3 = Matrix3::identity();
        let p_aa = self.covariance.fixed_view::<3, 3>(0, 0).into_owned();
        let p_ab = self.covariance.fixed_view::<3, 3>(0, 3).into_owned();

        let s = p_aa + self.star_tracker_noise_var * i3;
        let s_inv = match s.try_inverse() {
            Some(inv) => inv,
            None => {
                tracing::warn!("Star tracker update covariance singular");
                return;
            }
        };

        let k_att = p_aa * &s_inv;
        let k_bias = p_ab * &s_inv;

        let dx_att = &k_att * &delta_theta;
        let dx_bias = &k_bias * &delta_theta;

        let angle = dx_att.norm();
        if angle > 1e-12 {
            let axis = Unit::new_normalize(dx_att);
            let dq = UnitQuaternion::from_axis_angle(&axis, angle);
            self.state.quat = dq * self.state.quat;
        }

        self.state.gyro_bias += dx_bias;

        let mut kh = Matrix6::zeros();
        kh.fixed_view_mut::<3, 3>(0, 0).copy_from(&k_att);
        kh.fixed_view_mut::<3, 3>(3, 0).copy_from(&k_bias);

        let i6 = Matrix6::identity();
        self.covariance = (i6 - kh) * self.covariance;
    }

    pub fn get_state(&self) -> (UnitQuaternion<f64>, Vector3<f64>) {
        (self.state.quat, self.last_gyro_rate - self.state.gyro_bias)
    }
}
