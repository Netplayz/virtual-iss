#include "robotics/controller.h"
#include <algorithm>
#include <cmath>

namespace iss::robotics {

JointController::JointController(const PIDGains& gains, size_t dof) {
    gains_ = gains;
    integral_.assign(dof, 0.0);
    prev_error_.assign(dof, 0.0);
    initialized_ = true;
}

NewtonMeters JointController::compute(size_t idx, Radians desired, Radians current, Radians vel, double dt) {
    if (idx >= integral_.size()) return 0.0;

    double error = desired - current;
    integral_[idx] += error * dt;
    integral_[idx] = std::clamp(integral_[idx], -gains_.integral_limit, gains_.integral_limit);

    double derivative = (error - prev_error_[idx]) / std::max(dt, 1e-6);
    double output = gains_.kp * error + gains_.ki * integral_[idx] + gains_.kd * derivative;

    if (gains_.output_limit > 0) {
        output = std::clamp(output, -gains_.output_limit, gains_.output_limit);
    }

    prev_error_[idx] = error;
    return output;
}

NewtonMeters JointController::compute_cartesian(const CartesianState& desired, const CartesianState& current, const Eigen::MatrixXd& jacobian, double dt) {
    Eigen::VectorXd error(6);
    error(0) = desired.position.x - current.position.x;
    error(1) = desired.position.y - current.position.y;
    error(2) = desired.position.z - current.position.z;
    error(3) = desired.angular_velocity.x - current.angular_velocity.x;
    error(4) = desired.angular_velocity.y - current.angular_velocity.y;
    error(5) = desired.angular_velocity.z - current.angular_velocity.z;

    Eigen::VectorXd force = gains_.kp * error;

    Eigen::MatrixXd Jt = jacobian.transpose();
    Eigen::MatrixXd JJt = jacobian * Jt + Eigen::MatrixXd::Identity(6, 6) * 0.01;
    Eigen::VectorXd tau = Jt * (JJt.ldlt().solve(force));

    if (gains_.output_limit > 0) {
        double norm = tau.norm();
        if (norm > gains_.output_limit) {
            tau *= gains_.output_limit / norm;
        }
    }

    return tau(0);
}

void JointController::reset() {
    std::fill(integral_.begin(), integral_.end(), 0.0);
    std::fill(prev_error_.begin(), prev_error_.end(), 0.0);
}

void JointController::set_integral(size_t idx, double value) {
    if (idx < integral_.size()) integral_[idx] = value;
}

ComplianceController::ComplianceController(double stiffness, double damping)
    : stiffness_(stiffness), damping_(damping) {}

NewtonMeters ComplianceController::compute_force_control(const Vec3& desired, const Vec3& actual, const Vec3& vel, double dt) {
    (void)dt;
    Vec3 error;
    error.x = desired.x - actual.x;
    error.y = desired.y - actual.y;
    error.z = desired.z - actual.z;

    Vec3 damping;
    damping.x = -damping_ * vel.x;
    damping.y = -damping_ * vel.y;
    damping.z = -damping_ * vel.z;

    Vec3 output;
    output.x = stiffness_ * error.x + damping.x;
    output.y = stiffness_ * error.y + damping.y;
    output.z = stiffness_ * error.z + damping.z;

    return std::sqrt(output.x * output.x + output.y * output.y + output.z * output.z);
}

} // namespace iss::robotics
