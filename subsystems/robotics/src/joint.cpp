#include "robotics/joint.h"
#include <algorithm>
#include <cmath>

namespace iss::robotics {

Joint::Joint(JointId id, Radians min, Radians max, double friction, double damping) {
    state_.id = id;
    state_.position_min = min;
    state_.position_max = max;
    state_.friction = friction;
    state_.damping = damping;
    state_.position = 0.0;
    state_.velocity = 0.0;
    state_.torque = 0.0;
}

void Joint::set_position(Radians pos) {
    state_.position = clamp(pos);
}

void Joint::set_velocity(Radians vel) {
    state_.velocity = vel;
}

void Joint::set_torque(NewtonMeters tau) {
    state_.torque = tau;
}

bool Joint::at_limit() const {
    return state_.position <= state_.position_min ||
           state_.position >= state_.position_max;
}

Radians Joint::clamp(Radians pos) const {
    return std::clamp(pos, state_.position_min, state_.position_max);
}

bool Joint::in_range(Radians pos) const {
    return pos >= state_.position_min && pos <= state_.position_max;
}

void Joint::update(double dt, NewtonMeters command_torque) {
    double friction_torque = -state_.friction * std::tanh(state_.velocity * 100.0);
    double damping_torque = -state_.damping * state_.velocity;
    double net_torque = command_torque + friction_torque + damping_torque;

    state_.velocity += net_torque * dt;
    state_.position += state_.velocity * dt;
    state_.position = clamp(state_.position);

    if (at_limit()) {
        state_.velocity = 0.0;
    }

    state_.torque = net_torque;
}

void Joint::set_limits(Radians min, Radians max) {
    state_.position_min = min;
    state_.position_max = max;
}

} // namespace iss::robotics
