#pragma once

#include "types.h"
#include <vector>
#include <functional>

namespace iss::robotics {

struct Waypoint {
    CartesianState pose;
    Seconds time_from_start{0.0};
};

struct TrajectoryPoint {
    Seconds time{0.0};
    std::vector<double> joint_positions;
    std::vector<double> joint_velocities;
    std::vector<double> joint_accelerations;
};

class TrajectoryPlanner {
public:
    TrajectoryPlanner() = default;

    std::vector<TrajectoryPoint>
    plan_joint_space(const std::vector<double>& start_joints,
                     const std::vector<double>& end_joints,
                     Seconds duration,
                     double dt = 0.01) const;

    std::vector<TrajectoryPoint>
    plan_cartesian(const CartesianState& start,
                   const CartesianState& end,
                   const std::vector<double>& initial_joints,
                   Seconds duration,
                   double dt = 0.01) const;

    std::vector<TrajectoryPoint>
    plan_multi_waypoint(const std::vector<Waypoint>& waypoints,
                        const std::vector<double>& initial_joints,
                        double dt = 0.01) const;

    static double quintic_poly(double t, double t_f,
                                double q0, double q_f,
                                double v0 = 0, double v_f = 0,
                                double a0 = 0, double a_f = 0);

    static std::vector<double> blend_velocities(
        const std::vector<TrajectoryPoint>& seg1,
        const std::vector<TrajectoryPoint>& seg2,
        double blend_time);

private:
    TrajectoryPoint evaluate_segment(double t, double t_f,
                                      const double* start_q,
                                      const double* end_q, size_t n) const;
};

} // namespace iss::robotics
