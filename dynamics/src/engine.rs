use crate::attitude::{
    integrate_attitude, torque_aerodynamic, torque_gravity_gradient, torque_magnetic,
    AttitudeState, Quaternion,
};
use crate::environment::{self, atmosphere_density, earth_magnetic_field_eci, eclipse_check, solar_flux, sun_position_eci};
use crate::orbit::{beta_angle, eci_to_geodetic, OrbitPropagator, OrbitState, SGP4Propagator};
use crate::vehicle::VehicleConfig;
use anyhow::Result;
use async_nats::Client;
use futures::StreamExt;
use nalgebra::{Matrix3, Vector3};
use serde::{Deserialize, Serialize};

/// Top-level dynamics state published as telemetry.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DynamicsState {
    pub orbit: OrbitState,
    pub attitude: AttitudeState,
    pub eclipse: bool,
    pub beta_angle: f64,
    pub altitude: f64,
    pub solar_flux: f64,
    pub timestamp: f64,
}

/// Main dynamics engine that propagates orbit and attitude in real-time.
pub struct DynamicsEngine {
    propagator: SGP4Propagator,
    orbit_state: OrbitState,
    attitude_state: AttitudeState,
    vehicle: VehicleConfig,
    nats_client: Client,
}

impl DynamicsEngine {
    pub fn new(nats_client: Client, vehicle: VehicleConfig) -> Self {
        let initial_orbit = OrbitState {
            epoch: 2451545.0,
            sma: 6_780_000.0,
            ecc: 0.001,
            inc: 51.6_f64.to_radians(),
            raan: 0.0,
            argper: 0.0,
            truan: 0.0,
        };

        let initial_attitude = AttitudeState {
            quat: Quaternion::identity(),
            angular_rate: Vector3::new(0.0, 0.0, 0.001),
        };

        Self {
            propagator: SGP4Propagator::new(initial_orbit),
            orbit_state: initial_orbit,
            attitude_state: initial_attitude,
            vehicle,
            nats_client,
        }
    }

    /// Run the engine loop, subscribing to orchestrator ticks and command uplinks.
    pub async fn run(&mut self) -> Result<()> {
        let mut tick_sub = self.nats_client.subscribe("orchestrator.tick").await?;
        let mut cmd_sub = self.nats_client.subscribe("command.uplink").await?;

        loop {
            tokio::select! {
                Some(tick) = tick_sub.next() => {
                    let dt: f64 = serde_json::from_slice(&tick.payload).unwrap_or(1.0);
                    if let Err(e) = self.step(dt).await {
                        tracing::error!("Step error: {}", e);
                    }
                }
                Some(cmd) = cmd_sub.next() => {
                    if let Err(e) = self.handle_command(&cmd.payload).await {
                        tracing::error!("Command error: {}", e);
                    }
                }
            }
        }
    }

    /// Advance the simulation by dt seconds.
    async fn step(&mut self, dt: f64) -> Result<()> {
        let jd = self.orbit_state.epoch;

        // Propagate orbit
        self.orbit_state = *self.propagator.propagate(dt);

        // Convert Keplerian to ECI
        let (pos_eci, vel_eci) = self.kep_to_eci(&self.orbit_state);

        // Environmental models
        let (_lat, _lon, alt) = eci_to_geodetic(pos_eci, jd);
        let density = atmosphere_density(alt);
        let b_field = earth_magnetic_field_eci(pos_eci);
        let sun_pos = sun_position_eci(jd);
        let in_eclipse = eclipse_check(pos_eci, sun_pos);
        let orbit_normal = pos_eci.cross(&vel_eci).normalize();
        let sun_vec = (sun_pos - pos_eci).normalize();
        let beta = beta_angle(sun_vec, orbit_normal);
        let sflux = solar_flux(pos_eci, sun_pos);

        // Compute environmental torques
        let gg_torque = torque_gravity_gradient(pos_eci, self.vehicle.inertia);
        let aero_torque = torque_aerodynamic(
            vel_eci,
            density,
            self.vehicle.cp_offset,
            self.vehicle.area,
            self.vehicle.drag_coefficient,
        );
        let mag_torque = torque_magnetic(self.vehicle.residual_magnetic, b_field);
        let total_torque = gg_torque + aero_torque + mag_torque;

        // Integrate attitude using principal moments of inertia
        let inertia_vec = Vector3::new(
            self.vehicle.inertia[(0, 0)],
            self.vehicle.inertia[(1, 1)],
            self.vehicle.inertia[(2, 2)],
        );
        self.attitude_state = integrate_attitude(&self.attitude_state, total_torque, inertia_vec, dt);

        // Publish telemetry
        let state = DynamicsState {
            orbit: self.orbit_state,
            attitude: self.attitude_state.clone(),
            eclipse: in_eclipse,
            beta_angle: beta,
            altitude: alt,
            solar_flux: sflux,
            timestamp: jd,
        };

        let payload = serde_json::to_vec(&state)?;
        self.nats_client
            .publish("telemetry.dynamics.state", payload.into())
            .await?;

        tracing::debug!(
            "Step dt={}s alt={:.0}m eclipse={} beta={:.2}deg",
            dt,
            alt,
            in_eclipse,
            beta.to_degrees(),
        );

        Ok(())
    }

    /// Handle an uplink command.
    async fn handle_command(&mut self, payload: &[u8]) -> Result<()> {
        #[derive(Deserialize)]
        struct Command {
            action: String,
            target_quat: Option<[f64; 4]>,
            target_rate: Option<[f64; 3]>,
        }

        let cmd: Command = serde_json::from_slice(payload)?;
        match cmd.action.as_str() {
            "SET_ATTITUDE" => {
                if let Some(q) = cmd.target_quat {
                    self.attitude_state.quat = Quaternion(q);
                }
                if let Some(r) = cmd.target_rate {
                    self.attitude_state.angular_rate = Vector3::new(r[0], r[1], r[2]);
                }
                tracing::info!("Attitude set: q={:?} rate={:?}", self.attitude_state.quat.0, self.attitude_state.angular_rate);
            }
            other => {
                tracing::warn!("Unknown command action: {}", other);
            }
        }

        Ok(())
    }

    /// Convert Keplerian orbital elements to ECI position and velocity.
    fn kep_to_eci(&self, state: &OrbitState) -> (Vector3<f64>, Vector3<f64>) {
        let mu = environment::earth_gravitational_constant();
        let sma = state.sma;
        let ecc = state.ecc;
        let ta = state.truan;

        let p = sma * (1.0 - ecc * ecc);
        let r = p / (1.0 + ecc * ta.cos());
        let r_pqw = Vector3::new(r * ta.cos(), r * ta.sin(), 0.0);

        let h = (mu * p).sqrt();
        let v_pqw = Vector3::new(
            -mu / h * ta.sin(),
            mu / h * (ecc + ta.cos()),
            0.0,
        );

        let ra = state.raan;
        let ap = state.argper;
        let inc = state.inc;

        let c_ra = ra.cos();
        let s_ra = ra.sin();
        let c_ap = ap.cos();
        let s_ap = ap.sin();
        let c_i = inc.cos();
        let s_i = inc.sin();

        let rot = Matrix3::new(
            c_ra * c_ap - s_ra * s_ap * c_i,
            -c_ra * s_ap - s_ra * c_ap * c_i,
            s_ra * s_i,
            s_ra * c_ap + c_ra * s_ap * c_i,
            -s_ra * s_ap + c_ra * c_ap * c_i,
            -c_ra * s_i,
            s_ap * s_i,
            c_ap * s_i,
            c_i,
        );

        (rot * r_pqw, rot * v_pqw)
    }
}
