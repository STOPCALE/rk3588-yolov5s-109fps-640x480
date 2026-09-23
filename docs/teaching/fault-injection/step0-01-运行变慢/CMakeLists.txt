cmake_minimum_required(VERSION 3.16)
project(step0_fault_slow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(work main.cpp)

# 安装规则（本练习项目用，与故障无关）
install(TARGETS work RUNTIME DESTINATION .)
