use anyhow::{bail, Result};
use nalgebra::{Matrix3, SMatrix, Vector3};

type Matrix3x4 = SMatrix<f64, 3, 4>;

const SKEW_ANGLE_RAD: f64 = 0.9553;
const CMG_MOMENTUM_NMS: f64 = 3500.0;
const GIMBAL_RATE_LIMIT: f64 = 0.5;
const SINGULARITY_DETECT_THRESHOLD: f64 = 1e-6;
const MOMENTUM_UNLOAD_THRESHOLD_NMS: f64 = 12000.0;

#[derive(Debug, Clone)]
pub struct CMGCluster {
    pub gimbal_angles: [f64; 4],
    pub gimbal_rates: [f64; 4],
    pub momentum_magnitude: f64,
    pub skew_angle: f64,
}

impl CMGCluster {
    pub fn new() -> Self {
        CMGCluster {
            gimbal_angles: [0.0; 4],
            gimbal_rates: [0.0; 4],
            momentum_magnitude: CMG_MOMENTUM_NMS,
            skew_angle: SKEW_ANGLE_RAD,
        }
    }

    pub fn gimbal_axes(&self) -> [Vector3<f64>; 4] {
        let sa = self.skew_angle.sin();
        let ca = self.skew_angle.cos();
        [
            Vector3::new(0.0, -ca, sa),
            Vector3::new(-ca, 0.0, -sa),
            Vector3::new(0.0, ca, -sa),
            Vector3::new(ca, 0.0, sa),
        ]
    }

    pub fn transverse_axes(&self) -> [Vector3<f64>; 4] {
        [
            Vector3::new(1.0, 0.0, 0.0),
            Vector3::new(0.0, 1.0, 0.0),
            Vector3::new(-1.0, 0.0, 0.0),
            Vector3::new(0.0, -1.0, 0.0),
        ]
    }

    pub fn compute_momentum_from_gimbals(&self, gimbal_angles: &[f64; 4]) -> Vector3<f64> {
        let g = self.gimbal_axes();
        let t = self.transverse_axes();
        let mut total = Vector3::zeros();

        for i in 0..4 {
            let n = g[i].cross(&t[i]);
            total += self.momentum_magnitude
                * (gimbal_angles[i].cos() * t[i] + gimbal_angles[i].sin() * n);
        }

        total
    }

    pub fn compute_jacobian(&self, gimbal_angles: &[f64; 4]) -> Matrix3x4 {
        let g = self.gimbal_axes();
        let t = self.transverse_axes();
        let mut a = Matrix3x4::zeros();

        for i in 0..4 {
            let n = g[i].cross(&t[i]);
            let col = self.momentum_magnitude
                * (-gimbal_angles[i].sin() * t[i] + gimbal_angles[i].cos() * n);
            a.set_column(i, &col);
        }

        a
    }

    pub fn steer_for_torque(
        &mut self,
        requested_torque: Vector3<f64>,
        current_gimbals: &[f64; 4],
    ) -> Result<[f64; 4]> {
        let a = self.compute_jacobian(current_gimbals);
        let aat = a * a.transpose();

        let det = aat.determinant();
        if det.abs() < SINGULARITY_DETECT_THRESHOLD {
            bail!(
                "CMG cluster near singularity: det={}",
                det
            );
        }

        let aat_inv = match aat.try_inverse() {
            Some(inv) => inv,
            None => {
                let lambda = 0.01;
                let reg: Matrix3<f64> = aat + lambda * Matrix3::identity();
                match reg.try_inverse() {
                    Some(inv) => inv,
                    None => bail!("CMG pseudoinverse singular even with regularization"),
                }
            }
        };

        let gimbal_rates = a.transpose() * aat_inv * requested_torque;

        let mut clamped = [0.0_f64; 4];
        for i in 0..4 {
            clamped[i] = gimbal_rates[i].clamp(-GIMBAL_RATE_LIMIT, GIMBAL_RATE_LIMIT);
        }

        Ok(clamped)
    }

    pub fn check_singularity(&self, gimbal_angles: &[f64; 4]) -> bool {
        let a = self.compute_jacobian(gimbal_angles);
        let aat = a * a.transpose();
        let det = aat.determinant();
        det.abs() < SINGULARITY_DETECT_THRESHOLD
    }

    pub fn momentum_unload_threshold() -> f64 {
        MOMENTUM_UNLOAD_THRESHOLD_NMS
    }
}
