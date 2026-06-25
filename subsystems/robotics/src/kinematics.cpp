#include "robotics/kinematics.h"
#include <cmath>
#include <stdexcept>

namespace iss::robotics {

Kinematics::Kinematics(const std::vector<DHParameter>& dh_params)
    : dh_params_(dh_params) {}

Eigen::Matrix4d Kinematics::dh_transform(const DHParameter& dh, double theta) const {
    double ct = std::cos(theta);
    double st = std::sin(theta);
    double ca = std::cos(dh.alpha);
    double sa = std::sin(dh.alpha);

    Eigen::Matrix4d T;
    T << ct,   -st*ca,  st*sa,   dh.a * ct,
         st,    ct*ca,  -ct*sa,  dh.a * st,
         0,     sa,     ca,      dh.d,
         0,     0,      0,       1;

    return T;
}

Eigen::Matrix4d Kinematics::forward_joint(const std::vector<double>& joint_angles,
                                           int joint_index) const {
    if (joint_index < 0 || joint_index >= static_cast<int>(dh_params_.size())) {
        throw std::out_of_range("joint_index out of range");
    }

    Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
    for (int i = 0; i <= joint_index; ++i) {
        double theta = dh_params_[i].is_revolute ? joint_angles[i] : dh_params_[i].theta;
        T = T * dh_transform(dh_params_[i], theta);
    }
    return T;
}

Eigen::Matrix4d Kinematics::forward(const std::vector<double>& joint_angles) const {
    if (joint_angles.size() != dh_params_.size()) {
        throw std::invalid_argument("joint_angles size mismatch");
    }

    Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
    for (size_t i = 0; i < dh_params_.size(); ++i) {
        double theta = dh_params_[i].is_revolute ? joint_angles[i] : dh_params_[i].theta;
        T = T * dh_transform(dh_params_[i], theta);
    }
    return T;
}

CartesianState Kinematics::forward_state(
    const std::vector<double>& joint_angles,
    const std::vector<double>& joint_velocities) const
{
    CartesianState state;
    Eigen::Matrix4d T = forward(joint_angles);

    state.position.x = T(0, 3);
    state.position.y = T(1, 3);
    state.position.z = T(2, 3);

    Eigen::Matrix3d R = T.block<3,3>(0,0);
    double trace = R(0,0) + R(1,1) + R(2,2);
    if (trace > 0) {
        double s = 0.5 / std::sqrt(trace + 1.0);
        state.orientation.w = 0.25 / s;
        state.orientation.x = (R(2,1) - R(1,2)) * s;
        state.orientation.y = (R(0,2) - R(2,0)) * s;
        state.orientation.z = (R(1,0) - R(0,1)) * s;
    }

    auto [lin_vel, ang_vel] = compute_tip_velocity(joint_angles, joint_velocities);

    state.velocity.x = lin_vel(0);
    state.velocity.y = lin_vel(1);
    state.velocity.z = lin_vel(2);
    state.angular_velocity.x = ang_vel(0);
    state.angular_velocity.y = ang_vel(1);
    state.angular_velocity.z = ang_vel(2);

    return state;
}

Eigen::MatrixXd Kinematics::jacobian(const std::vector<double>& joint_angles) const {
    size_t n = dh_params_.size();
    Eigen::Matrix4d T = Eigen::Matrix4d::Identity();

    std::vector<Eigen::Vector3d> o(n);
    std::vector<Eigen::Vector3d> z(n);
    std::vector<Eigen::Vector3d> p(n);

    for (size_t i = 0; i < n; ++i) {
        double theta = dh_params_[i].is_revolute ? joint_angles[i] : dh_params_[i].theta;
        T = T * dh_transform(dh_params_[i], theta);

        z[i] = Eigen::Vector3d(T(0,2), T(1,2), T(2,2));
        p[i] = Eigen::Vector3d(T(0,3), T(1,3), T(2,3));
        o[i] = p[i];
    }

    Eigen::Matrix4d T_final = forward(joint_angles);
    Eigen::Vector3d p_end(T_final(0,3), T_final(1,3), T_final(2,3));

    Eigen::MatrixXd J(6, n);
    for (size_t i = 0; i < n; ++i) {
        if (dh_params_[i].is_revolute) {
            Eigen::Vector3d Jv = z[i].cross(p_end - o[i]);
            J(0, i) = Jv(0);
            J(1, i) = Jv(1);
            J(2, i) = Jv(2);
            J(3, i) = z[i](0);
            J(4, i) = z[i](1);
            J(5, i) = z[i](2);
        } else {
            J(0, i) = z[i](0);
            J(1, i) = z[i](1);
            J(2, i) = z[i](2);
            J(3, i) = 0; J(4, i) = 0; J(5, i) = 0;
        }
    }

    return J;
}

std::pair<Eigen::Vector3d, Eigen::Vector3d>
Kinematics::compute_tip_velocity(const std::vector<double>& joint_angles,
                                  const std::vector<double>& joint_velocities) const
{
    Eigen::MatrixXd J = jacobian(joint_angles);
    Eigen::VectorXd qd(joint_velocities.size());

    for (size_t i = 0; i < joint_velocities.size(); ++i) {
        qd(i) = joint_velocities[i];
    }

    Eigen::VectorXd twist = J * qd;
    return {
        Eigen::Vector3d(twist(0), twist(1), twist(2)),
        Eigen::Vector3d(twist(3), twist(4), twist(5))
    };
}

std::vector<double> Kinematics::inverse(
    const Eigen::Matrix4d& target,
    const std::vector<double>& initial_guess,
    double tolerance, int max_iterations) const
{
    size_t n = dh_params_.size();
    std::vector<double> q = initial_guess;

    if (q.size() != n) {
        q.assign(n, 0.0);
    }

    for (int iter = 0; iter < max_iterations; ++iter) {
        Eigen::Matrix4d T_current = forward(q);

        Eigen::Vector3d p_current(T_current(0,3), T_current(1,3), T_current(2,3));
        Eigen::Vector3d p_target(target(0,3), target(1,3), target(2,3));
        Eigen::Vector3d dp = p_target - p_current;

        Eigen::Matrix3d R_current = T_current.block<3,3>(0,0);
        Eigen::Matrix3d R_target = target.block<3,3>(0,0);
        Eigen::Matrix3d dR = R_target * R_current.transpose();

        double trace = dR(0,0) + dR(1,1) + dR(2,2);
        double angle = std::acos(std::clamp((trace - 1.0) / 2.0, -1.0, 1.0));
        Eigen::Vector3d axis;
        if (angle > 1e-6) {
            axis << dR(2,1) - dR(1,2),
                    dR(0,2) - dR(2,0),
                    dR(1,0) - dR(0,1);
            axis /= 2.0 * std::sin(angle);
            axis *= angle;
        } else {
            axis.setZero();
        }

        Eigen::MatrixXd J = jacobian(q);
        Eigen::VectorXd error(6);
        error << dp, axis;

        if (error.norm() < tolerance) {
            break;
        }

        Eigen::MatrixXd J_pinv = J.transpose() * (J * J.transpose() +
            Eigen::MatrixXd::Identity(6, 6) * 0.01).inverse();

        Eigen::VectorXd dq = J_pinv * error;

        for (size_t i = 0; i < n; ++i) {
            q[i] += dq(i);
        }
    }

    return q;
}

double Kinematics::manipulability(const std::vector<double>& joint_angles) const {
    Eigen::MatrixXd J = jacobian(joint_angles);
    Eigen::MatrixXd JJt = J * J.transpose();
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(JJt);
    double w = 1.0;
    for (int i = 0; i < es.eigenvalues().size(); ++i) {
        if (es.eigenvalues()(i) > 0) {
            w *= std::sqrt(es.eigenvalues()(i));
        }
    }
    return w;
}

bool Kinematics::is_singular(const std::vector<double>& joint_angles,
                              double threshold) const
{
    return manipulability(joint_angles) < threshold;
}

} // namespace iss::robotics
