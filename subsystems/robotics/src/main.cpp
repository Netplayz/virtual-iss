#include "robotics/ssrms.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

std::atomic<bool> running{true};

void signal_handler(int) {
    running = false;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    iss::robotics::SSRMS arm;
    arm.initialize();

    std::cout << "ISS Robotics Daemon v1.0.0" << std::endl;
    std::cout << "SSRMS initialized with " << arm.dof() << " DOF" << std::endl;

    constexpr double dt = 0.01; // 100 Hz control loop
    auto previous = std::chrono::steady_clock::now();

    while (running) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - previous).count();

        if (elapsed >= dt) {
            previous = now;

            arm.update(dt);

            std::cout << "\r[SSRMS] state=" << static_cast<int>(arm.arm_state())
                      << " mode=" << static_cast<int>(arm.control_mode())
                      << " power=" << arm.telemetry().power_consumption_w << "W    "
                      << std::flush;
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    arm.command_idle();
    std::cout << "\nRobotics daemon shutting down." << std::endl;
    return 0;
}
