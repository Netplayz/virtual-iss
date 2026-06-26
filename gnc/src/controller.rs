use std::collections::HashMap;

use anyhow::Result;
use chrono::Utc;
use futures::StreamExt;
use nalgebra::Vector3;
use serde::{Deserialize, Serialize};


use crate::control::{AttitudeController, ControlMode};
use crate::estimator::ExtendedKalmanFilter;
use crate::sensors::SensorSuite;
use crate::steering::CMGCluster;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TickMessage {
    pub sim_time_sec: f64,
    pub real_time_sec: f64,
    pub rate: f64,
    pub tick_number: i64,
    pub scenario_id: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Vector3Msg {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

impl From<Vector3Msg> for Vector3<f64> {
    fn from(v: Vector3Msg) -> Self {
        Vector3::new(v.x, v.y, v.z)
    }
}

impl From<Vector3<f64>> for Vector3Msg {
    fn from(v: Vector3<f64>) -> Self {
        Vector3Msg {
            x: v.x,
            y: v.y,
            z: v.z,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DynamicsStateMsg {
    pub timestamp: String,
    pub sim_time_sec: f64,
    pub position_eci_m: Vector3Msg,
    pub velocity_eci_ms: Vector3Msg,
    pub quaternion: [f64; 4],
    pub angular_rate_rads: Vector3Msg,
    pub beta_angle_deg: f64,
    pub altitude_m: f64,
    pub latitude_deg: f64,
    pub longitude_deg: f64,
    pub in_sun: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct GNCStateMsg {
    pub timestamp: String,
    pub attitude_est: [f64; 4],
    pub rate_est: Vector3Msg,
    pub control_torque_nm: Vector3Msg,
    pub cmg_momentum_nms: Vector3Msg,
    pub cmg_gimbal_angles: [f64; 4],
    pub control_mode: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CommandMsg {
    pub id: String,
    pub source: String,
    pub target: String,
    pub opcode: String,
    pub args: HashMap<String, serde_json::Value>,
    pub priority: i64,
    pub timestamp: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GNCFaultState {
    pub star_tracker_failed: bool,
    pub gyro_drift_rads: [f64; 3],
    pub sun_sensor_failed: bool,
    pub gps_failed: bool,
}

pub struct GNCController {
    pub nc: async_nats::Client,
    pub estimator: ExtendedKalmanFilter,
    pub controller: AttitudeController,
    pub cmg: CMGCluster,
    pub sensor_suite: SensorSuite,
    pub control_mode: ControlMode,
    pub target_attitude: [f64; 4],
    pub cmg_gimbals: [f64; 4],
    pub latest_dynamics: Option<DynamicsStateMsg>,
    pub last_tick_time: f64,
    pub fault_state: GNCFaultState,
}

impl GNCController {
    pub fn new(nc: async_nats::Client) -> Self {
        GNCController {
            nc,
            estimator: ExtendedKalmanFilter::new(),
            controller: AttitudeController::new(),
            cmg: CMGCluster::new(),
            sensor_suite: SensorSuite::new(),
            control_mode: ControlMode::InertialHold,
            target_attitude: [1.0, 0.0, 0.0, 0.0],
            cmg_gimbals: [0.0; 4],
            latest_dynamics: None,
            last_tick_time: 0.0,
            fault_state: GNCFaultState {
                star_tracker_failed: false,
                gyro_drift_rads: [0.0; 3],
                sun_sensor_failed: false,
                gps_failed: false,
            },
        }
    }

    pub async fn run(
        &mut self,
        mut tick_rx: async_nats::Subscriber,
    ) -> Result<()> {
        let mut dyn_sub = self.nc.subscribe("telemetry.dynamics.state").await?;
        let mut cmd_sub = self.nc.subscribe("command.uplink").await?;

        tracing::info!("GNC controller entering main loop");

        loop {
            tokio::select! {
                Some(tick_msg) = tick_rx.next() => {
                    if let Err(e) = self.handle_tick(&tick_msg).await {
                        tracing::error!(error = %e, "Error processing tick");
                    }
                }
                Some(dyn_msg) = dyn_sub.next() => {
                    if let Ok(ds) = serde_json::from_slice::<DynamicsStateMsg>(&dyn_msg.payload) {
                        self.latest_dynamics = Some(ds);
                    }
                }
                Some(cmd_msg) = cmd_sub.next() => {
                    if let Ok(cmd) = serde_json::from_slice::<CommandMsg>(&cmd_msg.payload) {
                        if let Err(e) = self.handle_command(&cmd).await {
                            tracing::error!(error = %e, cmd_id = %cmd.id, "Error handling command");
                        }
                    }
                }
                else => {
                    tracing::warn!("All NATS subscriptions closed");
                    break;
                }
            }
        }

        Ok(())
    }

    async fn handle_tick(&mut self, msg: &async_nats::Message) -> Result<()> {
        let tick: TickMessage = serde_json::from_slice(&msg.payload)?;
        let sim_time = tick.sim_time_sec;

        let dt = if self.last_tick_time > 0.0 {
            (sim_time - self.last_tick_time).max(0.001)
        } else {
            0.1
        };
        self.last_tick_time = sim_time;

        let dyn_state = match &self.latest_dynamics {
            Some(d) => d.clone(),
            None => {
                tracing::debug!("No dynamics state available yet, skipping tick");
                return Ok(());
            }
        };

        let true_quat = &dyn_state.quaternion;
        let true_rate = Vector3::new(
            dyn_state.angular_rate_rads.x,
            dyn_state.angular_rate_rads.y,
            dyn_state.angular_rate_rads.z,
        );
        let true_pos = Vector3::new(
            dyn_state.position_eci_m.x,
            dyn_state.position_eci_m.y,
            dyn_state.position_eci_m.z,
        );
        let true_vel = Vector3::new(
            dyn_state.velocity_eci_ms.x,
            dyn_state.velocity_eci_ms.y,
            dyn_state.velocity_eci_ms.z,
        );

        let sun_body = Vector3::new(1.0, 0.0, 0.0);
        let mag_body = Vector3::new(0.0, 0.0, 1.0);

        let (_st, gyro, _sun, _mag, _gps) = self.sensor_suite.measure_all(
            true_quat,
            true_rate,
            true_pos,
            true_vel,
            sun_body,
            mag_body,
        );

        let gyro_drift = Vector3::new(
            self.fault_state.gyro_drift_rads[0],
            self.fault_state.gyro_drift_rads[1],
            self.fault_state.gyro_drift_rads[2],
        );
        let gyro_drifted = crate::sensors::GyroOutput {
            angular_rate: gyro.angular_rate + gyro_drift,
            ..gyro
        };
        self.estimator.update_gyro(&gyro_drifted);
        self.estimator.predict(dt);

        if let Some(ds) = &self.latest_dynamics {
            if !self.fault_state.star_tracker_failed {
                let st_noise_std = self.sensor_suite.star_tracker.noise_stddev_arcsec;
                let noise_rad = st_noise_std * (1.0 / 3600.0) * (std::f64::consts::PI / 180.0);

                let angle_noise = self.sensor_suite.star_tracker.rng.gaussian(noise_rad);
                let axis_noise = Vector3::new(
                    self.sensor_suite.star_tracker.rng.gaussian(1.0),
                    self.sensor_suite.star_tracker.rng.gaussian(1.0),
                    self.sensor_suite.star_tracker.rng.gaussian(1.0),
                )
                .normalize();

                let true_q = crate::array_to_quat(&ds.quaternion);
                let noise_q = nalgebra::UnitQuaternion::from_axis_angle(
                    &nalgebra::Unit::new_normalize(axis_noise),
                    angle_noise,
                );
                let measured_q = noise_q * true_q;

                let st_output = crate::sensors::StarTrackerOutput {
                    quat_estimate: crate::quat_to_array(&measured_q),
                    timestamp: Utc::now().to_rfc3339(),
                    error_arcsec: angle_noise.abs() * (180.0 / std::f64::consts::PI) * 3600.0,
                };

                self.estimator.update_star_tracker(&st_output);
            } else {
                tracing::warn!("Star tracker faulted — skipping attitude update");
            }
        }

        let (est_quat, est_rate) = self.estimator.get_state();
        let est_quat_arr = crate::quat_to_array(&est_quat);

        let torque = match self.control_mode {
            ControlMode::Idle => Vector3::zeros(),
            ControlMode::InertialHold => {
                self.controller
                    .compute_control_torque(&self.target_attitude, &est_quat_arr, est_rate, dt)
            }
            ControlMode::SunPointing => {
                let target =
                    crate::control::compute_sun_pointing_target(Vector3::new(1.0, 0.0, 0.0));
                self.controller.compute_control_torque(&target, &est_quat_arr, est_rate, dt)
            }
            ControlMode::EarthPointing => {
                let target =
                    crate::control::compute_nadir_pointing_target(true_pos);
                self.controller.compute_control_torque(&target, &est_quat_arr, est_rate, dt)
            }
            ControlMode::Slew => {
                self.controller
                    .compute_control_torque(&self.target_attitude, &est_quat_arr, est_rate, dt)
            }
            ControlMode::MomentumUnload => {
                let current_momentum = self.cmg.compute_momentum_from_gimbals(&self.cmg_gimbals);
                let unload_torque = -0.01 * current_momentum;
                let target = crate::control::compute_nadir_pointing_target(true_pos);
                let nadir_torque = self.controller.compute_control_torque(
                    &target,
                    &est_quat_arr,
                    est_rate,
                    dt,
                );
                nadir_torque + unload_torque
            }
        };

        let gimbal_cmd = self
            .cmg
            .steer_for_torque(torque, &self.cmg_gimbals)?;

        for i in 0..4 {
            self.cmg_gimbals[i] = (self.cmg_gimbals[i] + gimbal_cmd[i] * dt).rem_euclid(2.0 * std::f64::consts::PI);
        }

        let cmg_momentum = self.cmg.compute_momentum_from_gimbals(&self.cmg_gimbals);

        let gnc_state = GNCStateMsg {
            timestamp: Utc::now().to_rfc3339(),
            attitude_est: est_quat_arr,
            rate_est: Vector3Msg::from(est_rate),
            control_torque_nm: Vector3Msg::from(torque),
            cmg_momentum_nms: Vector3Msg::from(cmg_momentum),
            cmg_gimbal_angles: self.cmg_gimbals,
            control_mode: self.control_mode.as_str().to_string(),
        };

        let payload = serde_json::to_vec(&gnc_state)?;
        self.nc
            .publish("telemetry.gnc.state", payload.into())
            .await?;

        Ok(())
    }

    async fn handle_command(&mut self, cmd: &CommandMsg) -> Result<()> {
        if cmd.target != "gnc" && cmd.target != "all" {
            return Ok(());
        }

        tracing::info!(opcode = %cmd.opcode, cmd_id = %cmd.id, "Processing GNC command");

        match cmd.opcode.as_str() {
            "SET_CONTROL_MODE" => {
                if let Some(mode_str) = cmd.args.get("mode").and_then(|v| v.as_str()) {
                    if let Some(mode) = ControlMode::from_str_option(mode_str) {
                        self.control_mode = mode;
                        self.controller.reset_integral();
                        tracing::info!(mode = %mode_str, "Control mode changed");
                    }
                }
            }
            "SET_TARGET_ATTITUDE" => {
                if let Some(quat) = cmd.args.get("quaternion").and_then(|v| v.as_array()) {
                    if quat.len() == 4 {
                        let mut arr = [0.0_f64; 4];
                        for (i, val) in quat.iter().enumerate() {
                            if let Some(n) = val.as_f64() {
                                arr[i] = n;
                            }
                        }
                        self.target_attitude = arr;
                        tracing::info!(target = ?self.target_attitude, "Target attitude updated");
                    }
                }
            }
            "SLEW" => {
                if let Some(quat) = cmd.args.get("target_attitude").and_then(|v| v.as_array()) {
                    if quat.len() == 4 {
                        let mut arr = [0.0_f64; 4];
                        for (i, val) in quat.iter().enumerate() {
                            if let Some(n) = val.as_f64() {
                                arr[i] = n;
                            }
                        }
                        self.target_attitude = arr;
                        self.control_mode = ControlMode::Slew;
                        self.controller.reset_integral();
                        tracing::info!(target = ?self.target_attitude, "Slew command accepted");
                    }
                }
            }
            "INJECT_FAULT" => {
                if let Some(fault_type) = cmd.args.get("type").and_then(|v| v.as_str()) {
                    tracing::warn!(fault_type = %fault_type, "Injecting GNC fault");
                    match fault_type {
                        "star_tracker_failure" => {
                            self.fault_state.star_tracker_failed = true;
                        }
                        "gyro_drift" => {
                            if let Some(drift) = cmd.args.get("drift_rads").and_then(|v| v.as_array()) {
                                if drift.len() == 3 {
                                    for (i, val) in drift.iter().enumerate() {
                                        if let Some(n) = val.as_f64() {
                                            self.fault_state.gyro_drift_rads[i] = n;
                                        }
                                    }
                                }
                            } else {
                                self.fault_state.gyro_drift_rads = [0.01; 3];
                            }
                        }
                        "sun_sensor_failure" => {
                            self.fault_state.sun_sensor_failed = true;
                        }
                        "gps_failure" => {
                            self.fault_state.gps_failed = true;
                        }
                        _ => {
                            tracing::warn!(fault_type, "Unknown GNC fault type");
                        }
                    }
                }
            }
            "CLEAR_FAULT" => {
                if let Some(fault_type) = cmd.args.get("type").and_then(|v| v.as_str()) {
                    match fault_type {
                        "star_tracker_failure" => self.fault_state.star_tracker_failed = false,
                        "gyro_drift" => self.fault_state.gyro_drift_rads = [0.0; 3],
                        "sun_sensor_failure" => self.fault_state.sun_sensor_failed = false,
                        "gps_failure" => self.fault_state.gps_failed = false,
                        _ => {
                            self.fault_state = GNCFaultState {
                                star_tracker_failed: false,
                                gyro_drift_rads: [0.0; 3],
                                sun_sensor_failed: false,
                                gps_failed: false,
                            };
                        }
                    }
                    tracing::info!(fault_type = %fault_type, "GNC fault cleared");
                }
            }
            _ => {
                tracing::warn!(opcode = %cmd.opcode, "Unknown GNC command opcode");
            }
        }

        Ok(())
    }
}
