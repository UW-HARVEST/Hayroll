cmake_minimum_required(VERSION 3.10)
project(Hayroll LANGUAGES C CXX)

# Clang and LLVM

#===============================================================================
# 1. VERIFY CLANG INSTALLATION DIR
#===============================================================================
# Set this to a valid Clang installation dir
#set(CT_LLVM_INSTALL_DIR "" CACHE PATH "LLVM installation directory")
set(CT_LLVM_INSTALL_DIR "/usr")

# A bit of a sanity checking
set(CT_LLVM_INCLUDE_DIR "${CT_LLVM_INSTALL_DIR}/include/llvm-14")
if(NOT EXISTS "${CT_LLVM_INCLUDE_DIR}")
message(FATAL_ERROR
  " CT_LLVM_INSTALL_DIR (${CT_LLVM_INCLUDE_DIR}) is invalid.")
endif()

set(CT_LLVM_CMAKE_FILE "${CT_LLVM_INSTALL_DIR}/lib/cmake/clang-14/ClangConfig.cmake")
if(NOT EXISTS "${CT_LLVM_CMAKE_FILE}")
message(FATAL_ERROR
  " CT_LLVM_CMAKE_FILE (${CT_LLVM_CMAKE_FILE}) is invalid.")
endif()

#===============================================================================
# 2. LOAD CLANG CONFIGURATION
#    For more: http://llvm.org/docs/CMake.html#embedding-llvm-in-your-project
#===============================================================================
set(CLANG_VERSION 14)
list(APPEND CMAKE_PREFIX_PATH "${CT_LLVM_INSTALL_DIR}/lib/cmake/llvm-${CLANG_VERSION}/")
list(APPEND CMAKE_PREFIX_PATH "${CT_LLVM_INSTALL_DIR}/lib/cmake/clang-${CLANG_VERSION}/")

find_package(Clang REQUIRED CONFIG)

# Sanity check. As Clang does not expose e.g. `CLANG_VERSION_MAJOR` through
# AddClang.cmake, we have to use LLVM_VERSION_MAJOR instead.
# TODO: Revisit when next version is released.
if(NOT "${CLANG_VERSION}" VERSION_EQUAL "${LLVM_VERSION_MAJOR}")
  message(FATAL_ERROR "Found LLVM ${LLVM_VERSION_MAJOR}, but need LLVM ${CLANG_VERSION}")
endif()

set(CLANG_C_COMPILER "clang-${CLANG_VERSION}")
message(STATUS "Found Clang ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using ClangConfig.cmake in: ${CT_LLVM_INSTALL_DIR}")


message("CLANG STATUS:
  Includes (clang)    ${CLANG_INCLUDE_DIRS}
  Includes (llvm)     ${LLVM_INCLUDE_DIRS}"
)

# Set the LLVM and Clang header and library paths
include_directories(SYSTEM "${LLVM_INCLUDE_DIRS};${CLANG_INCLUDE_DIRS}")

# Z3
find_package(Z3
  REQUIRED
  CONFIG
)

message(STATUS "Z3_FOUND: ${Z3_FOUND}")
message(STATUS "Found Z3 ${Z3_VERSION_STRING}")
message(STATUS "Z3_DIR: ${Z3_DIR}")

# awk
find_program(AWK_EXECUTABLE awk REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

add_executable(seeder src/seeder.cpp)

add_executable(pioneer src/pioneer.cpp)
target_include_directories(pioneer PRIVATE ${Z3_CXX_INCLUDE_DIRS})
target_link_libraries(pioneer PRIVATE ${Z3_LIBRARIES})
target_compile_options(pioneer PRIVATE ${Z3_COMPONENT_CXX_FLAGS})

add_executable(remove-comments src/RemoveComments.cpp)
target_link_libraries(remove-comments
    clangTooling
    clangBasic
    clangAST
    clangLex
    clangRewrite
    clangFrontend
    clangDriver
    LLVMSupport
)

add_executable(remove-non-preprocessor src/RemoveNonPreprocessor.cpp)
target_link_libraries(remove-non-preprocessor
    clangTooling
    clangBasic
    clangAST
    clangLex
    clangRewrite
    clangFrontend
    clangDriver
    LLVMSupport
)

add_executable(macro-skeleton src/MacroSkeleton.cpp)
target_link_libraries(macro-skeleton
    clangTooling
    clangBasic
    clangAST
    clangLex
    clangRewrite
    clangFrontend
    clangDriver
    LLVMSupport
)

# Rust

add_custom_target(
    analyzer ALL
    COMMAND CARGO_TARGET_DIR=${CMAKE_SOURCE_DIR}/build/ cargo build
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Rust analyzer binary"
)
