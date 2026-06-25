#include "robotics/trajectory.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace iss::robotics {

double TrajectoryPlanner::quintic_poly(double t, double t_f, double q0, double q_f, double v0, double v_f, double a0, double a_f) {
    if (t_f <= 0) return q_f;

    double tc = std::clamp(t / t_f, 0.0, 1.0);
    double tc2 = tc * tc;
    double tc3 = tc2 * tc;
    double tc4 = tc3 * tc;
    double tc5 = tc4 * tc;

    double diff = q_f - q0;

    double a0_t = v0 * t_f;
    double a1_t = a0 * t_f * t_f / 2.0;
    double a2_t = v_f * t_f;
    double a3_t = a_f * t_f * t_f / 2.0;

    double c0 = q0;
    double c1 = v0 * t_f;
    double c2 = a0 * t_f * t_f / 2.0;
    double c3 = 20.0 * diff - (8.0 * a2_t + 12.0 * a0_t) - (3.0 * a1_t - a3_t);
    double c4 = -30.0 * diff + (14.0 * a2_t + 16.0 * a0_t) + (3.0 * a1_t - 2.0 * a3_t);
    double c5 = 12.0 * diff - (6.0 * a2_t + 6.0 * a0_t) - (a1_t - a3_t);

    return c0 + c1 * tc + c2 * tc2 + c3 * tc3 + c4 * tc4 + c5 * tc5;
}

TrajectoryPoint TrajectoryPlanner::evaluate_segment(double t, double t_f, const double* start_q, const double* end_q, size_t n) const {
    TrajectoryPoint pt;
    pt.time = t;
    pt.joint_positions.resize(n);
    pt.joint_velocities.resize(n);
    pt.joint_accelerations.resize(n);

    double tc = std::clamp(t / t_f, 0.0, 1.0);
    double h = 1.0 / std::max(t_f, 1e-6);

    for (size_t i = 0; i < n; ++i) {
        double q0 = start_q[i], qf = end_q[i];
        pt.joint_positions[i] = quintic_poly(t, t_f, q0, qf);

        double tc2 = tc * tc;
        double tc3 = tc2 * tc;
        double tc4 = tc3 * tc;
        double diff = qf - q0;

        double v = 30.0 * diff * (tc2 - 2.0 * tc3 + tc4) * h;
        pt.joint_velocities[i] = v;
        pt.joint_accelerations[i] = 60.0 * diff * (tc - 3.0 * tc2 + 2.0 * tc3) * h * h;
    }

    return pt;
}

std::vector<TrajectoryPoint> TrajectoryPlanner::plan_joint_space(
    const std::vector<double>& start_joints,
    const std::vector<double>& end_joints,
    Seconds duration, double dt) const
{
    size_t n = start_joints.size();
    std::vector<TrajectoryPoint> trajectory;

    for (double t = 0; t <= duration + dt * 0.5; t += dt) {
        trajectory.push_back(evaluate_segment(t, duration, start_joints.data(), end_joints.data(), n));
    }

    return trajectory;
}

std::vector<TrajectoryPoint> TrajectoryPlanner::plan_cartesian(
    const CartesianState& start, const CartesianState& end,
    const std::vector<double>& initial_joints,
    Seconds duration, double dt) const
{
    (void)start; (void)end; (void)initial_joints; (void)duration; (void)dt;
    return {};
}

std::vector<TrajectoryPoint> TrajectoryPlanner::plan_multi_waypoint(
    const std::vector<Waypoint>& waypoints,
    const std::vector<double>& initial_joints,
    double dt) const
{
    (void)waypoints; (void)initial_joints; (void)dt;
    return {};
}

std::vector<double> TrajectoryPlanner::blend_velocities(
    const std::vector<TrajectoryPoint>& seg1,
    const std::vector<TrajectoryPoint>& seg2,
    double blend_time)
{
    (void)seg1; (void)seg2; (void)blend_time;
    return {};
}

} // namespace iss::robotics
