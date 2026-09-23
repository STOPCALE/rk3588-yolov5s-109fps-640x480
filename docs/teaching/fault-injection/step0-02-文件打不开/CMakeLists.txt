cmake_minimum_required(VERSION 3.16)
project(step0_fault_model LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(reader main.cpp)

# 安装到本项目内的 install/ 目录
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "" FORCE)
install(TARGETS reader RUNTIME DESTINATION .)
install(DIRECTORY model/ DESTINATION .)
