cmake_minimum_required(VERSION 3.16)
project(step4_fault_camfps LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_executable(camfps camfps.cc)
target_include_directories(camfps PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(camfps ${OpenCV_LIBS})
