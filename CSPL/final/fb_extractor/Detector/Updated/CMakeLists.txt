cmake_minimum_required(VERSION 3.22)
project(realtime_epilepsy_monitor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-O3 -pthread")

# Find OpenCV
find_package(OpenCV REQUIRED)

# Add executable
add_executable(realtime_monitor realtime_epilepsy_monitor.cpp)

# Include directories
include_directories(${OpenCV_INCLUDE_DIRS})

# Link libraries
target_link_libraries(realtime_monitor ${OpenCV_LIBS} pthread)

# Install target
install(TARGETS realtime_monitor DESTINATION bin)
