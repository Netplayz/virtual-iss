use nalgebra::Vector3;
use serde::{Deserialize, Serialize};

/// Keplerian orbital state at a given epoch.
#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct OrbitState {
    pub epoch: f64,
    pub sma: f64,
    pub ecc: f64,
    pub inc: f64,
    pub raan: f64,
    pub argper: f64,
    pub truan: f64,
}

/// Trait for orbit propagation.
pub trait OrbitPropagator {
    fn propagate(&mut self, dt_s: f64) -> &OrbitState;
}

/// Simplified SGP4 propagator with J2 secular perturbations.
pub struct SGP4Propagator {
    state: OrbitState,
}

impl SGP4Propagator {
    pub fn new(state: OrbitState) -> Self {
        Self { state }
    }

    pub fn state(&self) -> &OrbitState {
        &self.state
    }
}

impl OrbitPropagator for SGP4Propagator {
    fn propagate(&mut self, dt_s: f64) -> &OrbitState {
        let mu = crate::environment::earth_gravitational_constant();
        let j2 = crate::environment::j2_perturbation();
        let re = 6378137.0;
        let s = self.state;

        let n = (mu / s.sma.powi(3)).sqrt();
        let p = s.sma * (1.0 - s.ecc * s.ecc);
        let factor = 1.5 * n * j2 * (re / p).powi(2);

        let raan_dot = -factor * s.inc.cos();
        let argper_dot = factor * (2.5 * s.inc.sin().powi(2) - 2.0);
        let mean_anomaly_dot = n;

        self.state = OrbitState {
            epoch: s.epoch + dt_s / 86400.0,
            sma: s.sma,
            ecc: s.ecc,
            inc: s.inc,
            raan: s.raan + raan_dot * dt_s,
            argper: s.argper + argper_dot * dt_s,
            truan: s.truan + mean_anomaly_dot * dt_s,
        };

        &self.state
    }
}

/// Convert ECI position to geodetic coordinates (lat, lon, alt) in radians and meters.
pub fn eci_to_geodetic(pos: Vector3<f64>, jd: f64) -> (f64, f64, f64) {
    let re = 6378137.0;
    let gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0);
    let gmst_rad = gmst.to_radians();

    let x_ecef = pos.x * gmst_rad.cos() + pos.y * gmst_rad.sin();
    let y_ecef = -pos.x * gmst_rad.sin() + pos.y * gmst_rad.cos();
    let z_ecef = pos.z;

    let lon = y_ecef.atan2(x_ecef);
    let p = (x_ecef * x_ecef + y_ecef * y_ecef).sqrt();
    let lat = z_ecef.atan2(p);
    let alt = pos.norm() - re;

    (lat, lon, alt.max(0.0))
}

/// Compute the beta angle (angle between the sun vector and the orbit plane) in radians.
pub fn beta_angle(sun_vector: Vector3<f64>, orbit_normal: Vector3<f64>) -> f64 {
    let s = sun_vector.normalize();
    let n = orbit_normal.normalize();
    (std::f64::consts::FRAC_PI_2 - s.dot(&n).acos()).abs()
}
