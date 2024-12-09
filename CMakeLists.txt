cmake_minimum_required(VERSION 3.10)

project(MyProject LANGUAGES CXX)

find_program(AWK_EXECUTABLE awk)
if(NOT AWK_EXECUTABLE)
    message(FATAL_ERROR "awk not found! Please install awk to proceed.")
else()
    message(STATUS "Found awk: ${AWK_EXECUTABLE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

add_executable(hoe src/hoe.cpp)

# add_executable(App2 src/app2.cpp)
