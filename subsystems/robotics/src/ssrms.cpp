#include "robotics/ssrms.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace iss::robotics {

SSRMS::SSRMS() {
    kinematics_ = std::make_unique<Kinematics>(make_ssrms_dh_params());
    limits_ = make_limits();
    joint_controller_ = std::make_unique<JointController>(
        PIDGains{50.0, 1.0, 5.0, 10.0, 200.0}, 7);
    compliance_controller_ = std::make_unique<ComplianceController>(100.0, 10.0);
    planner_ = std::make_unique<TrajectoryPlanner>();

    state_.joints.resize(7);
    joints_.resize(7);

    static const double joint_limits[7][2] = {
        {-2.96, 2.96}, {-2.09, 2.09}, {-2.96, 2.96},
        {-2.09, 2.09}, {-2.96, 2.96}, {-2.09, 2.09},
        {-2.96, 2.96}
    };

    for (int i = 0; i < 7; ++i) {
        joints_[i].id = i;
        joints_[i].position = 0.0;
        joints_[i].velocity = 0.0;
        joints_[i].torque = 0.0;
        joints_[i].position_min = joint_limits[i][0];
        joints_[i].position_max = joint_limits[i][1];
        joints_[i].friction = 0.02 + 0.01 * i;
        joints_[i].damping = 0.1 + 0.05 * i;
    }
}

std::vector<DHParameter> SSRMS::make_ssrms_dh_params() {
    std::vector<DHParameter> params(7);
    // Canadarm2 / SSRMS DH parameters (approximate)
    params[0] = {0.0, -M_PI_2, 0.4, 0.0, true};   // Shoulder yaw
    params[1] = {0.0,  M_PI_2, 0.0, 0.0, true};   // Shoulder pitch
    params[2] = {0.0, -M_PI_2, 1.2, 0.0, true};   // Elbow pitch
    params[3] = {0.0,  M_PI_2, 0.0, 0.0, true};   // Wrist 1
    params[4] = {0.0, -M_PI_2, 1.2, 0.0, true};   // Wrist 2
    params[5] = {0.0,  M_PI_2, 0.0, 0.0, true};   // Wrist 3
    params[6] = {0.0,  0.0,    0.4, 0.0, true};   // Wrist roll
    return params;
}

ManipulatorLimits SSRMS::make_limits() {
    ManipulatorLimits limits;
    limits.max_joint_speed = 0.1;
    limits.max_tip_force = 100.0;
    limits.max_tip_torque = 50.0;
    limits.singularity_threshold = 0.01;
    return limits;
}

void SSRMS::initialize() {
    state_.state = ArmState::Standby;
    state_.mode = ControlMode::Hold;

    for (auto& j : joints_) {
        j.position = 0.0;
        j.velocity = 0.0;
        j.torque = 0.0;
    }

    state_.tip_pose = {};
    state_.manipulability = kinematics_->manipulability({0,0,0,0,0,0,0});
    state_.singularity = kinematics_->is_singular({0,0,0,0,0,0,0});
    state_.power_consumption_w = 50.0;
    trajectory_active_ = false;
}

void SSRMS::reset() {
    state_.state = ArmState::Uninitialized;
    state_.mode = ControlMode::Idle;
    trajectory_active_ = false;
    joint_controller_->reset();
    initialize();
}

void SSRMS::update(double dt) {
    if (state_.state == ArmState::Fault || state_.state == ArmState::Uninitialized) {
        return;
    }

    state_.timestamp_ms += static_cast<uint64_t>(dt * 1000.0);

    switch (state_.mode) {
        case ControlMode::JointPosition:
        case ControlMode::CartesianPosition:
        case ControlMode::TrajectoryFollowing:
            update_controller(dt);
            update_dynamics(dt);
            break;

        case ControlMode::ForceCompliance:
            update_controller(dt);
            update_dynamics(dt);
            break;

        case ControlMode::Hold:
            for (auto& j : joints_) {
                j.velocity = 0.0;
                j.torque = 0.0;
            }
            break;

        case ControlMode::GravityCompensation:
            // ISS is microgravity, minimal compensation needed
            update_dynamics(dt);
            break;

        default:
            break;
    }

    if (trajectory_active_) {
        trajectory_time_ += dt;
        if (trajectory_idx_ < current_trajectory_.size()) {
            double duration = current_trajectory_[trajectory_idx_].time_from_start;
            if (duration > 0 && trajectory_time_ >= duration) {
                trajectory_idx_++;
                trajectory_time_ = 0.0;
            }
        } else {
            trajectory_active_ = false;
        }
    }

    compute_manipulability();
    check_safety();
    enforce_limits();

    state_.power_consumption_w = power_consumption();

    // Update telemetry joint states
    for (size_t i = 0; i < 7 && i < state_.joints.size(); ++i) {
        state_.joints[i] = joints_[i];
    }

    // Update tip pose
    auto jp = joint_positions();
    auto jv = joint_velocities();
    state_.tip_pose = kinematics_->forward_state(jp, jv);
}

void SSRMS::set_control_mode(ControlMode mode) {
    if (state_.state == ArmState::Fault) return;

    state_.mode = mode;
    if (mode == ControlMode::Hold || mode == ControlMode::Idle) {
        trajectory_active_ = false;
    }
    joint_controller_->reset();
}

void SSRMS::command_joint_positions(const std::vector<double>& positions) {
    if (positions.size() == 7) {
        state_.mode = ControlMode::JointPosition;
        std::vector<double> target(7);
        for (size_t i = 0; i < 7; ++i) {
            target[i] = std::clamp(positions[i], joints_[i].position_min, joints_[i].position_max);
        }
        (void)target;
    }
}

void SSRMS::command_cartesian_pose(const CartesianState& pose) {
    state_.mode = ControlMode::CartesianPosition;
    target_pose_ = pose;
}

void SSRMS::command_trajectory(const std::vector<Waypoint>& waypoints) {
    if (waypoints.empty()) return;
    current_trajectory_ = waypoints;
    trajectory_idx_ = 0;
    trajectory_time_ = 0.0;
    trajectory_active_ = true;
    state_.mode = ControlMode::TrajectoryFollowing;
}

void SSRMS::command_force(Vec3 force, NewtonMeters torque) {
    state_.mode = ControlMode::ForceCompliance;
    state_.tip_force = force;
    state_.tip_torque = torque;
}

void SSRMS::command_brake() {
    trajectory_active_ = false;
    for (auto& j : joints_) {
        j.velocity = 0.0;
        j.torque = 0.0;
    }
    state_.state = ArmState::Braked;
    state_.mode = ControlMode::Idle;
}

void SSRMS::command_stow() {
    command_joint_positions({0, 0, 0, 0, 0, 0, 0});
    state_.state = ArmState::Stowed;
}

void SSRMS::command_idle() {
    trajectory_active_ = false;
    state_.mode = ControlMode::Idle;
    if (state_.state != ArmState::Fault) {
        state_.state = ArmState::Standby;
    }
}

void SSRMS::command_abort() {
    trajectory_active_ = false;
    command_brake();
    state_.state = ArmState::Fault;
}

void SSRMS::set_joint_position(size_t joint, Radians pos) {
    if (joint < joints_.size()) {
        joints_[joint].position = std::clamp(pos, joints_[joint].position_min, joints_[joint].position_max);
    }
}

void SSRMS::set_joint_velocity(size_t joint, Radians vel) {
    if (joint < joints_.size()) {
        joints_[joint].velocity = std::clamp(vel, -limits_.max_joint_speed, limits_.max_joint_speed);
    }
}

void SSRMS::set_tip_force(Vec3 f) {
    state_.tip_force = f;
}

void SSRMS::set_tip_torque(NewtonMeters t) {
    state_.tip_torque = t;
}

std::vector<double> SSRMS::joint_positions() const {
    std::vector<double> positions(7);
    for (size_t i = 0; i < 7; ++i) {
        positions[i] = joints_[i].position;
    }
    return positions;
}

std::vector<double> SSRMS::joint_velocities() const {
    std::vector<double> velocities(7);
    for (size_t i = 0; i < 7; ++i) {
        velocities[i] = joints_[i].velocity;
    }
    return velocities;
}

CartesianState SSRMS::tip_pose() const {
    auto jp = joint_positions();
    auto jv = joint_velocities();
    return kinematics_->forward_state(jp, jv);
}

double SSRMS::power_consumption() const {
    double power = 50.0; // base power

    for (const auto& j : joints_) {
        power += std::abs(j.torque * j.velocity);
        power += j.friction * std::abs(j.velocity) * 10.0;
    }

    if (state_.state == ArmState::Braked) power *= 0.3;
    if (state_.state == ArmState::Stowed) power *= 0.1;

    return power;
}

void SSRMS::update_dynamics(double dt) {
    if (dt <= 0) return;

    for (auto& j : joints_) {
        double friction_torque = -j.friction * std::tanh(j.velocity * 100.0);
        double damping_torque = -j.damping * j.velocity;
        double net_torque = j.torque + friction_torque + damping_torque;

        // Simplified inertia (I = 1.0 kg*m^2 per joint)
        j.velocity += net_torque * dt;
        j.position += j.velocity * dt;
        j.position = std::clamp(j.position, j.position_min, j.position_max);

        if (j.position <= j.position_min || j.position >= j.position_max) {
            j.velocity = 0.0;
        }
    }
}

void SSRMS::update_controller(double dt) {
    if (state_.mode == ControlMode::JointPosition) {
        auto jp = joint_positions();
        auto jv = joint_velocities();
        for (size_t i = 0; i < 7; ++i) {
            joints_[i].torque = joint_controller_->compute(i, 0.0, jp[i], jv[i], dt);
        }
    }
    else if (state_.mode == ControlMode::CartesianPosition) {
        auto jp = joint_positions();
        auto jv = joint_velocities();
        auto current = kinematics_->forward_state(jp, jv);
        auto J = kinematics_->jacobian(jp);
        joints_[0].torque = joint_controller_->compute_cartesian(target_pose_, current, J, dt);
    }
    else if (state_.mode == ControlMode::ForceCompliance) {
        Vec3 actual_force = state_.tip_force;
        auto jp = joint_positions();
        auto jv = joint_velocities();
        auto current = kinematics_->forward_state(jp, jv);
        Vec3 vel{current.velocity.x, current.velocity.y, current.velocity.z};
        joints_[0].torque = compliance_controller_->compute_force_control(actual_force, actual_force, vel, dt);
    }
}

void SSRMS::check_safety() {
    for (const auto& j : joints_) {
        if (std::abs(j.velocity) > limits_.max_joint_speed * 1.5) {
            state_.state = ArmState::Fault;
            return;
        }
    }

    if (state_.tip_force.norm() > limits_.max_tip_force * 1.5 ||
        std::abs(state_.tip_torque) > limits_.max_tip_torque * 1.5) {
        state_.state = ArmState::Fault;
        return;
    }
}

void SSRMS::enforce_limits() {
    for (auto& j : joints_) {
        j.position = std::clamp(j.position, j.position_min, j.position_max);
        if (j.position == j.position_min || j.position == j.position_max) {
            j.velocity = 0.0;
        }
    }
}

void SSRMS::compute_manipulability() {
    auto jp = joint_positions();
    state_.manipulability = kinematics_->manipulability(jp);
    state_.singularity = kinematics_->is_singular(jp, limits_.singularity_threshold);
}

std::string SSRMS::serialize_telemetry() const {
    std::ostringstream os;
    os << "{\"timestamp_ms\":" << state_.timestamp_ms
       << ",\"state\":" << static_cast<int>(state_.state)
       << ",\"mode\":" << static_cast<int>(state_.mode)
       << ",\"joints\":[";

    for (size_t i = 0; i < state_.joints.size(); ++i) {
        if (i > 0) os << ",";
        os << "{"
           << "\"id\":" << static_cast<int>(state_.joints[i].id)
           << ",\"pos\":" << state_.joints[i].position
           << ",\"vel\":" << state_.joints[i].velocity
           << ",\"torque\":" << state_.joints[i].torque
           << "}";
    }

    os << "],\"tip\":{"
       << "\"x\":" << state_.tip_pose.position.x
       << ",\"y\":" << state_.tip_pose.position.y
       << ",\"z\":" << state_.tip_pose.position.z
       << "},\"manipulability\":" << state_.manipulability
       << ",\"singularity\":" << (state_.singularity ? "true" : "false")
       << ",\"power_w\":" << state_.power_consumption_w
       << "}";

    return os.str();
}

bool SSRMS::deserialize_command(const std::string& json, ArmCommand& cmd) const {
    (void)json; (void)cmd;
    // JSON parsing would depend on a library (e.g., nlohmann/json)
    // Placeholder: parse command_id and opcode from simple format
    return false;
}

} // namespace iss::robotics
