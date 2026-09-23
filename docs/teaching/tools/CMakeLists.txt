cmake_minimum_required(VERSION 3.16)
project(teaching_probe_attrs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 本目录相对仓库根：docs/teaching/tools
set(REPO_ROOT ${CMAKE_SOURCE_DIR}/../../..)

add_executable(probe_attrs probe_attrs.cc)
target_include_directories(probe_attrs PRIVATE ${REPO_ROOT}/include)
link_directories(${REPO_ROOT}/install/my_rknn_yolov5_demo_aarch64/lib)
target_link_libraries(probe_attrs rknnrt pthread)
set_target_properties(probe_attrs PROPERTIES
    BUILD_RPATH "${REPO_ROOT}/install/my_rknn_yolov5_demo_aarch64/lib")
