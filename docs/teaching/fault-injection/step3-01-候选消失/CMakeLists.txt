cmake_minimum_required(VERSION 3.16)
project(step3_fault_cand LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cand cand_gone.cc)
target_compile_options(cand PRIVATE -O2)
