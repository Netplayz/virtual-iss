#include "robotics/joint.h"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    iss::robotics::Joint j(0, -2.96, 2.96, 0.01, 0.1);
    assert(j.id() == 0);
    assert(std::abs(j.position()) < 1e-12);

    j.set_position(1.0);
    assert(std::abs(j.position() - 1.0) < 1e-12);

    j.set_position(10.0);
    assert(std::abs(j.position() - 2.96) < 1e-12);

    j.set_position(-10.0);
    assert(std::abs(j.position() + 2.96) < 1e-12);

    assert(j.at_limit());

    assert(j.in_range(0.0));
    assert(!j.in_range(10.0));

    j.update(0.01, 10.0);
    assert(j.velocity() > 0);

    std::cout << "All joint tests passed." << std::endl;
    return 0;
}
