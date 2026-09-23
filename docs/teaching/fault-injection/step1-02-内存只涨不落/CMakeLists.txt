cmake_minimum_required(VERSION 3.16)
project(step1_fault_leak LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 在板子上构建：本目录相对仓库根为 docs/teaching/fault-injection/step1-02-内存只涨不落
set(REPO_ROOT ${CMAKE_SOURCE_DIR}/../../../..)

add_executable(leak leak.cc)
target_include_directories(leak PRIVATE ${REPO_ROOT}/include)
link_directories(${REPO_ROOT}/install/my_rknn_yolov5_demo_aarch64/lib)
target_link_libraries(leak rknnrt pthread)
set_target_properties(leak PROPERTIES
    BUILD_RPATH "${REPO_ROOT}/install/my_rknn_yolov5_demo_aarch64/lib")

install(TARGETS leak RUNTIME DESTINATION .)
