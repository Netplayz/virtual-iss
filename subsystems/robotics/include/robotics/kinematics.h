#pragma once

#include "types.h"
#include <Eigen/Dense>
#include <vector>

namespace iss::robotics {

struct DHParameter {
    double a{0.0};       // link length
    double alpha{0.0};   // link twist (rad)
    double d{0.0};       // link offset
    double theta{0.0};   // joint angle (rad) — variable for revolute
    bool is_revolute{true};
};

class Kinematics {
public:
    explicit Kinematics(const std::vector<DHParameter>& dh_params);

    Eigen::Matrix4d forward_joint(const std::vector<double>& joint_angles,
                                  int joint_index) const;

    Eigen::Matrix4d forward(const std::vector<double>& joint_angles) const;

    CartesianState forward_state(const std::vector<double>& joint_angles,
                                 const std::vector<double>& joint_velocities) const;

    std::vector<double> inverse(const Eigen::Matrix4d& target,
                                const std::vector<double>& initial_guess,
                                double tolerance = 1e-6,
                                int max_iterations = 100) const;

    Eigen::MatrixXd jacobian(const std::vector<double>& joint_angles) const;

    std::pair<Eigen::Vector3d, Eigen::Vector3d>
    compute_tip_velocity(const std::vector<double>& joint_angles,
                         const std::vector<double>& joint_velocities) const;

    double manipulability(const std::vector<double>& joint_angles) const;

    bool is_singular(const std::vector<double>& joint_angles,
                     double threshold = 0.01) const;

    size_t dof() const { return dh_params_.size(); }

    const std::vector<DHParameter>& params() const { return dh_params_; }

private:
    Eigen::Matrix4d dh_transform(const DHParameter& dh, double theta) const;

    std::vector<DHParameter> dh_params_;
};

} // namespace iss::robotics
