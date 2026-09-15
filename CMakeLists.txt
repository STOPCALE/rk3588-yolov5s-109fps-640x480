cmake_minimum_required(VERSION 3.16)

project(my_rknn_yolov5_demo VERSION 1.0.0 LANGUAGES C CXX)

# ---------- 语言标准 ----------
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)               # 用 -std=c++14, 而不是 gnu++14

# ---------- 构建类型 ----------
# CMAKE_BUILD_TYPE 为空时(单配置生成器的默认值), 所有 CMAKE_<LANG>_FLAGS_<CONFIG>
# 变量都不生效, 编出来就是 -O0, 所以必须先给默认值。
# 注: GCC 下 Release 默认就是 "-O3 -DNDEBUG", 无需再手工追加。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# 生成 compile_commands.json, 供 IDE / clangd 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------- 自包含部署包: 装到项目内 install/ ----------
# 用 CMAKE_SYSTEM_PROCESSOR 区分架构(x86_64 / aarch64), 避免不同架构产物互相覆盖
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install/${PROJECT_NAME}_${CMAKE_SYSTEM_PROCESSOR}"
    CACHE PATH "安装前缀" FORCE)

set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
# $ORIGIN 由动态加载器展开为"可执行文件自身所在目录"
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# ---------- 第三方依赖 ----------
# RKNN: 头文件与 librknnrt.so 都来自 include/
set(RKNN_RT_LIB "${CMAKE_SOURCE_DIR}/include/librknnrt.so")

# RGA: 库路径按目标架构自动选择, 不再硬编码 aarch64
set(RGA_PATH "${CMAKE_SOURCE_DIR}/include/3rdparty/rga/RK3588")
set(RGA_LIB  "${RGA_PATH}/lib/Linux/${CMAKE_SYSTEM_PROCESSOR}/librga.so")

# ---------- 依赖包 ----------
# 组件按"符号所在的库"来选:
#   core=Mat/Point  imgproc=绘图/缩放  highgui=imshow
#   videoio=VideoCapture  imgcodecs=imread/imwrite
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc highgui videoio imgcodecs)
find_package(Threads REQUIRED)

# ---------- 目标 ----------
add_executable(my_rknn_yolov5_demo
        src/main.cc
        # src/postprocess.cc        # 还没写的先注释掉(CMake 要求列出的文件必须存在)
        # src/preprocess.cc
        # src/rkYolov5s.cc
        # src/uart.c
)

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

# librknnrt/librga 内部引用了链接命令行上看不到的符号, 放宽共享库符号检查。
# 只对本目标生效(原来的全局 CMAKE_EXE_LINKER_FLAGS 会影响目录下所有可执行文件)
target_link_options(my_rknn_yolov5_demo PRIVATE -Wl,--allow-shlib-undefined)

# ---------- 安装: 产出可直接拷贝到板子的自包含目录 ----------
install(TARGETS my_rknn_yolov5_demo RUNTIME DESTINATION .)
install(FILES ${RKNN_RT_LIB} ${RGA_LIB} DESTINATION lib)
# 注意: model 后面不要加结尾斜杠, 否则会丢掉 model/ 这一层,
#       而 postprocess.cc 里硬编码的是 ./model/coco_80_labels_list.txt
install(DIRECTORY model DESTINATION .)