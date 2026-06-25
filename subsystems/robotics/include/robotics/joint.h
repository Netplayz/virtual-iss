#pragma once

#include "types.h"

namespace iss::robotics {

class Joint {
public:
    Joint() = default;
    Joint(JointId id, Radians min, Radians max, double friction, double damping);

    JointId id() const { return state_.id; }
    const JointState& state() const { return state_; }

    void set_position(Radians pos);
    void set_velocity(Radians vel);
    void set_torque(NewtonMeters tau);

    Radians position() const { return state_.position; }
    Radians velocity() const { return state_.velocity; }

    bool at_limit() const;
    Radians clamp(Radians pos) const;
    bool in_range(Radians pos) const;

    void update(double dt, NewtonMeters command_torque);

    void set_limits(Radians min, Radians max);
    void set_friction(double f) { state_.friction = f; }
    void set_damping(double d) { state_.damping = d; }

private:
    JointState state_;
};

} // namespace iss::robotics
