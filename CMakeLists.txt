cmake_minimum_required(VERSION 3.16)

project(my_rknn_yolov5_demo VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#这是优化级别选项
# 【必须先做】构建类型: CMAKE_BUILD_TYPE 为空时(单配置生成器的默认值),
# 下面所有 CMAKE_CXX_FLAGS_<CONFIG> 变量都不会被使用, 编出来就是 -O0。
# 所以这里必须先给默认值, 否则下面"优化级别"那行等于白写。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# 优化级别选项
# 注: GCC 下 Release 的默认值本来就是 "-O3 -DNDEBUG", 这里又拼了一遍,
#     实际命令行会变成 "-O3 -DNDEBUG -O3 -DNDEBUG"(无害但冗余)。
#     想追加额外开关请改成:
#     set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -funroll-loops")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--allow-shlib-undefined")

set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/my_rknn_yolov5_demo_${CMAKE_SYSTEM_NAME})

set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")#可以使用相对路径编译

#rknn api
set(LIB_ARCH aarch64)
set(RKNN_RT_LIB ${CMAKE_SOURCE_DIR}/include/librknnrt.so)

#rga
set(RGA_PATH ${CMAKE_SOURCE_DIR}/include/3rdparty/rga/RK3588)
set(RGA_LIB ${RGA_PATH}/lib/Linux/${LIB_ARCH}/librga.so)

#opencv
#使用有限个数的opencv模块, 避免opencv过大导致编译失败
#注: 组件要按"符号所在的库"来选, 不是按头文件:
#     core=Mat/Point, imgproc=绘图/缩放, highgui=imshow,
#     videoio=VideoCapture(摄像头/视频), imgcodecs=imread/imwrite
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc highgui videoio imgcodecs)

#threads
find_package(Threads REQUIRED)

#all

add_executable(my_rknn_yolov5_demo
        src/main.cc
        # src/postprocess.cc
        # src/preprocess.cc
        # src/rkYolov5s.cc
        # src/uart.c

)

#PRIVATE: 只对当前target生效
target_include_directories(my_rknn_yolov5_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${RGA_PATH}/include
)

target_link_libraries(my_rknn_yolov5_demo PRIVATE
        ${RKNN_RT_LIB}
        ${RGA_LIB}
        ${OpenCV_LIBS}
        Threads::Threads
)

install(TARGETS my_rknn_yolov5_demo DESTINATION .)
install(FILES ""
        ${RKNN_RT_LIB}
        ${RGA_LIB}
        DESTINATION lib
)

install(DIRECTORY model DESTINATION .)

