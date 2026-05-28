cmake_minimum_required(VERSION 3.14)
project(LFFilter CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O3")

include_directories(${CMAKE_SOURCE_DIR}/src)

add_executable(main
src/main.cc
src/common/util.cc)
