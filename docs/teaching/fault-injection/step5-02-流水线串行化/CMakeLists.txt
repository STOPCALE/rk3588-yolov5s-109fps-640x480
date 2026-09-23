cmake_minimum_required(VERSION 3.16)
project(step5_fault_pipeline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
add_executable(pipeline pipeline.cc)
target_link_libraries(pipeline Threads::Threads)
