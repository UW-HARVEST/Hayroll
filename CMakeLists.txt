cmake_minimum_required(VERSION 3.10)

project(Hayroll LANGUAGES CXX)

find_program(AWK_EXECUTABLE awk REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

add_executable(hoe src/hoe.cpp)

# Rust

add_custom_target(
    analyzer ALL
    COMMAND CARGO_TARGET_DIR=${CMAKE_SOURCE_DIR}/build/ cargo build
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Rust analyzer binary"
)
