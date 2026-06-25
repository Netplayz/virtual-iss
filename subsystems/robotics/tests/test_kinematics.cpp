#include "robotics/kinematics.h"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::vector<iss::robotics::DHParameter> params = {
        {0.0, -M_PI_2, 0.4, 0.0, true},
        {0.0,  M_PI_2, 0.0, 0.0, true},
    };

    iss::robotics::Kinematics kin(params);
    assert(kin.dof() == 2);

    std::vector<double> angles = {0.0, 0.0};
    auto T = kin.forward(angles);
    assert(std::abs(T(0,3)) < 1e-12);
    assert(std::abs(T(1,3)) < 1e-12);
    assert(std::abs(T(2,3) - 0.4) < 1e-12);

    auto J = kin.jacobian(angles);
    assert(J.rows() == 6);
    assert(J.cols() == 2);

    double mu = kin.manipulability(angles);
    assert(mu > 0);
    assert(!kin.is_singular(angles));

    std::vector<double> vel = {0.1, 0.0};
    auto jv = kin.forward_state(angles, vel);
    (void)jv;

    std::cout << "All kinematics tests passed." << std::endl;
    return 0;
}
