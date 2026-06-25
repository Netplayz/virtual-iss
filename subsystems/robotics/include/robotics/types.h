#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace iss::robotics {

using JointId = uint8_t;
using Seconds = double;
using Radians = double;
using Meters  = double;
using Newtons  = double;
using NewtonMeters = double;

struct Vec3 {
    double x{0.0}, y{0.0}, z{0.0};

    Vec3() = default;
    Vec3(double x, double y, double z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x};
    }
    double norm() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 normalized() const { double n = norm(); return n > 1e-12 ? *this * (1.0/n) : Vec3{}; }
};

struct Quaternion {
    double w{1.0}, x{0.0}, y{0.0}, z{0.0};

    Quaternion() = default;
    Quaternion(double w, double x, double y, double z) : w(w), x(x), y(y), z(z) {}
};

struct Transform {
    Vec3 position;
    Quaternion orientation;

    static Transform identity() { return {}; }
};

struct JointState {
    JointId id{};
    Radians position{0.0};
    Radians velocity{0.0};
    NewtonMeters torque{0.0};
    Radians position_min{-3.14159};
    Radians position_max{3.14159};
    double friction{0.01};
    double damping{0.1};
};

struct CartesianState {
    Vec3 position;
    Quaternion orientation;
    Vec3 velocity;
    Vec3 angular_velocity;
};

struct ManipulatorLimits {
    double max_joint_speed{0.1};      // rad/s
    double max_tip_force{100.0};       // N
    double max_tip_torque{50.0};       // Nm
    double singularity_threshold{0.01};
};

enum class ControlMode {
    Idle,
    JointPosition,
    CartesianPosition,
    ForceCompliance,
    GravityCompensation,
    Hold,
    TrajectoryFollowing
};

enum class ArmState {
    Uninitialized,
    Standby,
    Active,
    Braked,
    Fault,
    Stowed
};

struct ArmTelemetry {
    uint64_t timestamp_ms{0};
    ArmState state{ArmState::Uninitialized};
    ControlMode mode{ControlMode::Idle};
    std::vector<JointState> joints;
    CartesianState tip_pose;
    Vec3 tip_force;
    NewtonMeters tip_torque{0.0};
    double manipulability{0.0};
    bool singularity{false};
    double power_consumption_w{0.0};
};

struct ArmCommand {
    uint32_t cmd_id{0};
    uint8_t opcode{0};
    std::vector<double> params;
};

} // namespace iss::robotics
