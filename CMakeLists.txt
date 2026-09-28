# //限定最小的cmake版本
cmake_minimum_required(VERSION 3.16)

# //指定项目名称与项目涉及到的内容
project(study_rknn_yolo_demo VERSION 1.0.0 LANGUAGES C CXX)
# //这里要指定c++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# //对构建类型和配置生成器进行配置，有-O0和-O3的区别
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRING Debug Release RelwithDebInfo MinSizeRel)
endif()

# //生成COMPILE_COMMANDS开启
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# //安装路径指定，并且会分辨x86和aarch64
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install/${PROJECT_NAME}_${CMAKE_SYSTEM_PROCESSOR}"
    CACHE PATH "安装前缀" FORCE)

set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH)

# //把安装放在可执行目录里
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# //设定rknn库的路径
set(RKNN_RT_LIB "${CMAKE_SOURCE_DIR}/include/librknnrt.so")

# //自动选择目标路径
set(RGA_PATH "${CMAKE_SOURCE_DIR}/include/3rdparty/rga/RK3588")
set(RGA_LIB  "${RGA_PATH}/lib/Linux/${CMAKE_SYSTEM_PROCESSOR}/librga.so")

# //寻找所需的包
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc highgui videoio imgcodecs)
find_package(Threads REQUIRED)

# //设置共用文件
set(RKNN_COMMON_SRC
        # src/postrocess.cc
        # src/preprocess.cc
        # src/rkYolov5s.cc
)

# //主要程序的拓展
add_executable(study_rknn_yolo_demo
                src/main.cc
                # src/uart.c
                # src/trajectory_predictor.cc
                # ${RKNN_COMMON_SRC}
)

# # //回归验证工具
# add_executable(bench_throughput
#                 # src/bench_throughput.cc
#                 # ${RKNN_COMMON_SRC}
# )

# # //视频解码探针
# add_executable(probe_video
#         # src/probe_video.cc
# )

# # //摄像头诊断
# add_executable(cam_probe
#         # src/cam_probe.cc
# )

# # //摄像头录像工具
# add_executable(cam_record
#         # src/cam_record.cc
# )

# //依次使用循环将这些程序进行配置
foreach(tgt study_rknn_yolo_demo)

    target_include_directories(${tgt} PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${RGA_PATH}/include
    )

    target_link_libraries(${tgt} PRIVATE
            ${RKNN_RT_LIB}
            ${RGA_LIB}
            ${OpenCV_LIBS}
            Threads::Threads
    )

        target_link_options(${tgt} PRIVATE -Wl,--allow-shlib-undefined)

endforeach()

# //安装
install(TARGETS study_rknn_yolo_demo RUNTIME DESTINATION .)
install(FILES ${RKNN_RT_LIB} ${RGA_LIB} DESTINATION lib)
install(DIRECTORY model DESTINATION .)