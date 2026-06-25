use chrono::Utc;
use nalgebra::Vector3;
use serde::{Deserialize, Serialize};

pub struct SimpleRng {
    state: u64,
}

impl SimpleRng {
    pub fn new(seed: u64) -> Self {
        SimpleRng { state: seed }
    }

    fn next_u64(&mut self) -> u64 {
        const A: u64 = 6364136223846793005;
        const C: u64 = 1442695040888963407;
        self.state = self.state.wrapping_mul(A).wrapping_add(C);
        self.state
    }

    pub fn next_f64(&mut self) -> f64 {
        (self.next_u64() >> 11) as f64 / (1u64 << 53) as f64
    }

    pub fn gaussian(&mut self, stddev: f64) -> f64 {
        let u1 = self.next_f64();
        let u2 = self.next_f64();
        let z = (-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos();
        z * stddev
    }

    pub fn gaussian_vec3(&mut self, stddev: f64) -> Vector3<f64> {
        Vector3::new(
            self.gaussian(stddev),
            self.gaussian(stddev),
            self.gaussian(stddev),
        )
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StarTrackerOutput {
    pub quat_estimate: [f64; 4],
    pub timestamp: String,
    pub error_arcsec: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GyroOutput {
    pub angular_rate: Vector3<f64>,
    pub timestamp: String,
    pub bias: Vector3<f64>,
    pub noise: Vector3<f64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SunSensorOutput {
    pub sun_vector_body: Vector3<f64>,
    pub timestamp: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MagnetometerOutput {
    pub magnetic_field_body: Vector3<f64>,
    pub timestamp: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GPSOutput {
    pub position: Vector3<f64>,
    pub velocity: Vector3<f64>,
    pub timestamp: String,
}

pub struct SimulatedStarTracker {
    pub rng: SimpleRng,
    pub noise_stddev_arcsec: f64,
    pub update_rate_hz: f64,
}

impl SimulatedStarTracker {
    pub fn new(noise_stddev_arcsec: f64) -> Self {
        SimulatedStarTracker {
            rng: SimpleRng::new(42),
            noise_stddev_arcsec,
            update_rate_hz: 1.0,
        }
    }

    pub fn measure(&mut self, true_quat: &[f64; 4]) -> StarTrackerOutput {
        let noise_rad = self.noise_stddev_arcsec * (1.0 / 3600.0) * (std::f64::consts::PI / 180.0);
        let angle_noise = self.rng.gaussian(noise_rad);
        let axis_noise = Vector3::new(
            self.rng.gaussian(1.0),
            self.rng.gaussian(1.0),
            self.rng.gaussian(1.0),
        )
        .normalize();

        let true_q = crate::array_to_quat(true_quat);
        let noise_q = nalgebra::UnitQuaternion::from_axis_angle(
            &nalgebra::Unit::new_normalize(axis_noise),
            angle_noise,
        );
        let measured_q = noise_q * true_q;

        StarTrackerOutput {
            quat_estimate: crate::quat_to_array(&measured_q),
            timestamp: Utc::now().to_rfc3339(),
            error_arcsec: angle_noise.abs() * (180.0 / std::f64::consts::PI) * 3600.0,
        }
    }
}

pub struct SimulatedGyro {
    pub rng: SimpleRng,
    pub noise_stddev_rads: f64,
    pub bias_stddev_rads: f64,
    pub bias_walk_stddev_rads: f64,
    pub bias: Vector3<f64>,
}

impl SimulatedGyro {
    pub fn new(noise_stddev_rads: f64, bias_stddev_rads: f64) -> Self {
        let mut rng = SimpleRng::new(123);
        let bias = rng.gaussian_vec3(bias_stddev_rads);
        SimulatedGyro {
            rng,
            noise_stddev_rads,
            bias_stddev_rads,
            bias_walk_stddev_rads: bias_stddev_rads * 0.001,
            bias,
        }
    }

    pub fn measure(&mut self, true_rate: Vector3<f64>) -> GyroOutput {
        let noise = self.rng.gaussian_vec3(self.noise_stddev_rads);
        let measured_rate = true_rate + self.bias + noise;

        let output = GyroOutput {
            angular_rate: measured_rate,
            timestamp: Utc::now().to_rfc3339(),
            bias: self.bias,
            noise,
        };

        self.bias += self.rng.gaussian_vec3(self.bias_walk_stddev_rads);

        output
    }
}

pub struct SimulatedSunSensor {
    pub rng: SimpleRng,
    pub noise_stddev_deg: f64,
}

impl SimulatedSunSensor {
    pub fn new(noise_stddev_deg: f64) -> Self {
        SimulatedSunSensor {
            rng: SimpleRng::new(456),
            noise_stddev_deg,
        }
    }

    pub fn measure(&mut self, true_sun_vector_body: Vector3<f64>) -> SunSensorOutput {
        let noise_rad = self.noise_stddev_deg * (std::f64::consts::PI / 180.0);
        let noise_angle = self.rng.gaussian(noise_rad);
        let noise_axis = Vector3::new(
            self.rng.gaussian(1.0),
            self.rng.gaussian(1.0),
            self.rng.gaussian(1.0),
        )
        .normalize();
        let noise_q = nalgebra::UnitQuaternion::from_axis_angle(
            &nalgebra::Unit::new_normalize(noise_axis),
            noise_angle,
        );
        let measured = noise_q * true_sun_vector_body;

        SunSensorOutput {
            sun_vector_body: measured,
            timestamp: Utc::now().to_rfc3339(),
        }
    }
}

pub struct SimulatedMagnetometer {
    pub rng: SimpleRng,
    pub noise_stddev_nt: f64,
}

impl SimulatedMagnetometer {
    pub fn new(noise_stddev_nt: f64) -> Self {
        SimulatedMagnetometer {
            rng: SimpleRng::new(789),
            noise_stddev_nt,
        }
    }

    pub fn measure(&mut self, true_mag_field_body: Vector3<f64>) -> MagnetometerOutput {
        let noise = self.rng.gaussian_vec3(self.noise_stddev_nt * 1e-9);
        MagnetometerOutput {
            magnetic_field_body: true_mag_field_body + noise,
            timestamp: Utc::now().to_rfc3339(),
        }
    }
}

pub struct SimulatedGPS {
    pub rng: SimpleRng,
    pub pos_noise_stddev_m: f64,
    pub vel_noise_stddev_ms: f64,
}

impl SimulatedGPS {
    pub fn new(pos_noise_stddev_m: f64, vel_noise_stddev_ms: f64) -> Self {
        SimulatedGPS {
            rng: SimpleRng::new(101112),
            pos_noise_stddev_m,
            vel_noise_stddev_ms,
        }
    }

    pub fn measure(&mut self, true_pos: Vector3<f64>, true_vel: Vector3<f64>) -> GPSOutput {
        GPSOutput {
            position: true_pos + self.rng.gaussian_vec3(self.pos_noise_stddev_m),
            velocity: true_vel + self.rng.gaussian_vec3(self.vel_noise_stddev_ms),
            timestamp: Utc::now().to_rfc3339(),
        }
    }
}

pub struct SensorSuite {
    pub star_tracker: SimulatedStarTracker,
    pub gyro: SimulatedGyro,
    pub sun_sensor: SimulatedSunSensor,
    pub magnetometer: SimulatedMagnetometer,
    pub gps: SimulatedGPS,
}

impl SensorSuite {
    pub fn new() -> Self {
        SensorSuite {
            star_tracker: SimulatedStarTracker::new(5.0),
            gyro: SimulatedGyro::new(0.0001, 0.00001),
            sun_sensor: SimulatedSunSensor::new(0.5),
            magnetometer: SimulatedMagnetometer::new(100.0),
            gps: SimulatedGPS::new(10.0, 0.1),
        }
    }

    pub fn measure_all(
        &mut self,
        true_quat: &[f64; 4],
        true_rate: Vector3<f64>,
        true_pos: Vector3<f64>,
        true_vel: Vector3<f64>,
        true_sun_body: Vector3<f64>,
        true_mag_body: Vector3<f64>,
    ) -> (StarTrackerOutput, GyroOutput, SunSensorOutput, MagnetometerOutput, GPSOutput) {
        let st = self.star_tracker.measure(true_quat);
        let gyro = self.gyro.measure(true_rate);
        let sun = self.sun_sensor.measure(true_sun_body);
        let mag = self.magnetometer.measure(true_mag_body);
        let gps = self.gps.measure(true_pos, true_vel);
        (st, gyro, sun, mag, gps)
    }
}
