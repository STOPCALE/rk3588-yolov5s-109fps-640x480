cmake_minimum_required(VERSION 3.16)
project(step6_fault_pty LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# openpty 在 libutil 里
add_executable(pty_crlf pty_crlf.cc)
target_link_libraries(pty_crlf util)
