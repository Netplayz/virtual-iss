use anyhow::Result;
use nalgebra::{Matrix3, Vector3};
use serde::{Deserialize, Serialize};

/// Vehicle configuration for dynamics simulation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VehicleConfig {
    pub mass: f64,
    pub inertia: Matrix3<f64>,
    pub area: f64,
    pub drag_coefficient: f64,
    pub cp_offset: Vector3<f64>,
    pub residual_magnetic: Vector3<f64>,
}

impl Default for VehicleConfig {
    fn default() -> Self {
        Self::iss_default()
    }
}

impl VehicleConfig {
    /// Default ISS-like vehicle configuration.
    pub fn iss_default() -> Self {
        Self {
            mass: 419_725.0,
            inertia: Matrix3::new(
                15_000_000.0, 0.0, 0.0,
                0.0, 10_000_000.0, 0.0,
                0.0, 0.0, 5_000_000.0,
            ),
            area: 2400.0,
            drag_coefficient: 2.0,
            cp_offset: Vector3::new(1.0, 0.0, 0.0),
            residual_magnetic: Vector3::new(0.1, 0.1, 0.1),
        }
    }

    /// Load vehicle configuration from a JSON file.
    pub fn load_config(path: &str) -> Result<Self> {
        let content = std::fs::read_to_string(path)?;
        let config: VehicleConfig = serde_json::from_str(&content)?;
        Ok(config)
    }
}
