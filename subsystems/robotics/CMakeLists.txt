cmake_minimum_required(VERSION 3.20)
project(iss-robotics VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Eigen3 3.4 REQUIRED)

add_library(robotics
    src/kinematics.cpp
    src/joint.cpp
    src/controller.cpp
    src/trajectory.cpp
    src/ssrms.cpp
)

target_include_directories(robotics
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${EIGEN3_INCLUDE_DIR}
)

target_link_libraries(robotics
    PUBLIC
        Eigen3::Eigen
)

add_executable(roboticsd src/main.cpp)
target_link_libraries(roboticsd robotics)

install(TARGETS robotics roboticsd DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

enable_testing()
add_subdirectory(tests)
