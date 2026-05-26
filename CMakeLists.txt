cmake_minimum_required(VERSION 3.14)
project(SktechTime CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set(CMAKE_CXX_COMPILER clang)
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
# set(CMAKE_CXX_FLAGS "--driver-mode=g++")

# include_directories(/usr/local/include)
include_directories(${CMAKE_SOURCE_DIR}/src)
# include_directories(${FETCHCONTENT_BASE_DIR})
# link_directories(/usr/local/lib)

add_executable(main
src/main.cc
src/common/util.cc)
