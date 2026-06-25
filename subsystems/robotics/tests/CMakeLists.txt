add_executable(test_joint test_joint.cpp)
target_link_libraries(test_joint robotics)

add_executable(test_kinematics test_kinematics.cpp)
target_link_libraries(test_kinematics robotics)

add_test(NAME joint COMMAND test_joint)
add_test(NAME kinematics COMMAND test_kinematics)
