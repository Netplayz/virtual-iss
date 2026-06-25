#pragma once

#include "types.h"
#include <Eigen/Dense>
#include <vector>
#include <array>

namespace iss::robotics {

struct PIDGains {
    double kp{0.0};
    double ki{0.0};
    double kd{0.0};
    double integral_limit{0.0};
    double output_limit{0.0};
};

class JointController {
public:
    JointController() = default;
    JointController(const PIDGains& gains, size_t dof);

    void set_gains(const PIDGains& gains) { gains_ = gains; }
    const PIDGains& gains() const { return gains_; }

    NewtonMeters compute(size_t joint_index,
                         Radians desired_pos,
                         Radians current_pos,
                         Radians current_vel,
                         double dt);

    NewtonMeters compute_cartesian(const CartesianState& desired,
                                   const CartesianState& current,
                                   const Eigen::MatrixXd& jacobian,
                                   double dt);

    void reset();
    void set_integral(size_t joint_index, double value);

private:
    PIDGains gains_;
    std::vector<double> integral_;
    std::vector<double> prev_error_;
    bool initialized_{false};
};

class ComplianceController {
public:
    ComplianceController() = default;
    ComplianceController(double stiffness, double damping);

    NewtonMeters compute_force_control(const Vec3& desired_force,
                                       const Vec3& actual_force,
                                       const Vec3& tip_velocity,
                                       double dt);

    void set_stiffness(double k) { stiffness_ = k; }
    void set_damping(double d) { damping_ = d; }

private:
    double stiffness_{100.0};
    double damping_{10.0};
};

} // namespace iss::robotics
