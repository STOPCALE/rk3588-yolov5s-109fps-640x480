cmake_minimum_required(VERSION 3.16)
project(step1_fault_dims LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 在板子上构建：本目录相对仓库根为 docs/teaching/fault-injection/step1-01-维度读反
set(REPO_ROOT ${CMAKE_SOURCE_DIR}/../../../..)

add_executable(dims dims.cc)
target_include_directories(dims PRIVATE ${REPO_ROOT}/include)
link_directories(${REPO_ROOT}/install/my_rknn_yolov5_demo_aarch64/lib)
target_link_libraries(dims rknnrt pthread)
set_target_properties(dims PROPERTIES
    BUILD_RPATH "${REPO_ROOT}/install/my_rknn_yolov5_demo_aarch64/lib")

# 交付默认直接在这个目录下跑
install(TARGETS dims RUNTIME DESTINATION .)
