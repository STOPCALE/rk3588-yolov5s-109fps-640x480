cmake_minimum_required(VERSION 3.16)
project(step2_fault_restore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_executable(restore restore.cc)
target_include_directories(restore PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(restore ${OpenCV_LIBS})
