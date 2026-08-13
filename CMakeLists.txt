cmake_minimum_required(VERSION 3.20)
project(triangular_arbitrage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(triarb src/main.cpp)

target_compile_options(triarb PRIVATE -O3 -Wall -Wextra -Wpedantic)
target_compile_definitions(triarb PRIVATE TRIARB_VERSION="0.1.0")
target_link_libraries(triarb PRIVATE Threads::Threads)
