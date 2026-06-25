#pragma once

#include "kinematics.h"
#include "controller.h"
#include "trajectory.h"
#include "types.h"
#include <memory>
#include <vector>

namespace iss::robotics {

class SSRMS {
public:
    SSRMS();
    ~SSRMS() = default;

    void initialize();
    void update(double dt);
    void reset();

    void set_control_mode(ControlMode mode);
    ControlMode control_mode() const { return state_.mode; }
    ArmState arm_state() const { return state_.state; }

    void command_joint_positions(const std::vector<double>& positions);
    void command_cartesian_pose(const CartesianState& pose);
    void command_trajectory(const std::vector<Waypoint>& waypoints);
    void command_force(Vec3 force, NewtonMeters torque);
    void command_brake();
    void command_stow();
    void command_idle();
    void command_abort();

    void set_joint_position(size_t joint, Radians pos);
    void set_joint_velocity(size_t joint, Radians vel);
    void set_tip_force(Vec3 f);
    void set_tip_torque(NewtonMeters t);

    const ArmTelemetry& telemetry() const { return state_; }
    ArmTelemetry& telemetry() { return state_; }

    const Kinematics& kinematics() const { return *kinematics_; }

    std::vector<double> joint_positions() const;
    std::vector<double> joint_velocities() const;
    CartesianState tip_pose() const;

    size_t dof() const { return 7; }

    double power_consumption() const;

    // Telemetry serialization
    std::string serialize_telemetry() const;
    bool deserialize_command(const std::string& json, ArmCommand& cmd) const;

private:
    void update_dynamics(double dt);
    void update_controller(double dt);
    void check_safety();
    void enforce_limits();
    void compute_manipulability();

    static std::vector<DHParameter> make_ssrms_dh_params();
    static ManipulatorLimits make_limits();

    std::unique_ptr<Kinematics> kinematics_;
    std::unique_ptr<JointController> joint_controller_;
    std::unique_ptr<ComplianceController> compliance_controller_;
    std::unique_ptr<TrajectoryPlanner> planner_;

    ArmTelemetry state_;
    std::vector<JointState> joints_;
    CartesianState target_pose_;
    std::vector<Waypoint> current_trajectory_;
    size_t trajectory_idx_{0};
    Seconds trajectory_time_{0.0};

    ManipulatorLimits limits_;
    bool trajectory_active_{false};
};

} // namespace iss::robotics
