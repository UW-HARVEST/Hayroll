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

#===============================================================================
# 3. FIND CLANG EXECUTABLE
#===============================================================================
find_program(CLANG_EXE 
    NAMES clang-${CLANG_VERSION} clang
    HINTS "${CT_LLVM_INSTALL_DIR}/bin"
    DOC "Path to clang++ compiler"
)
if(NOT CLANG_EXE)
    message(FATAL_ERROR "clang executable not found. Check CT_LLVM_INSTALL_DIR or install Clang.")
endif()
message(STATUS "CLANG_EXE: ${CLANG_EXE}")

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
find_program(AWK_EXE awk REQUIRED)

# spdlog
find_package(spdlog REQUIRED)

message(STATUS "AWK_EXE: ${AWK_EXE}")

# tree-sitter
set(TREE_SITTER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tree-sitter")
set(TREE_SITTER_C_PREPROC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tree-sitter-c_preproc")
set(TREE_SITTER_INCLUDE_DIR "${TREE_SITTER_DIR}/lib/include")
set(TREE_SITTER_C_PREPROC_INCLUDE_DIR "${TREE_SITTER_C_PREPROC_DIR}/bindings/c")

message(STATUS "TREE_SITTER_DIR: ${TREE_SITTER_DIR}")
message(STATUS "TREE_SITTER_C_PREPROC_DIR: ${TREE_SITTER_C_PREPROC_DIR}")

# Common config
link_libraries(
    spdlog::spdlog
)

add_library(clang_tooling_config INTERFACE)
target_link_libraries(clang_tooling_config INTERFACE
    clangTooling
    clangBasic
    clangAST
    clangLex
    clangRewrite
    clangFrontend
    clangDriver
    LLVMSupport
)
target_compile_definitions(clang_tooling_config INTERFACE
    CLANG_EXE="${CLANG_EXE}"
)

add_library(z3_config INTERFACE)
target_include_directories(z3_config INTERFACE
    ${Z3_CXX_INCLUDE_DIRS}
)
target_compile_options(z3_config INTERFACE ${Z3_COMPONENT_CXX_FLAGS})
target_link_libraries(z3_config INTERFACE
    ${Z3_LIBRARIES}
)

add_library(tree_sitter_config INTERFACE)
target_include_directories(tree_sitter_config INTERFACE
    ${TREE_SITTER_INCLUDE_DIR}
    ${TREE_SITTER_C_PREPROC_INCLUDE_DIR}
)
target_link_libraries(tree_sitter_config INTERFACE
    ${TREE_SITTER_DIR}/libtree-sitter.a
    ${TREE_SITTER_C_PREPROC_DIR}/src/parser.o
)

# C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_definitions("-g")

include_directories(include)
include_directories(src)

add_executable(seeder src/seeder.cpp)

add_executable(pioneer src/pioneer.cpp)
target_link_libraries(pioneer PRIVATE
    clang_tooling_config
    z3_config
    tree_sitter_config
)

add_executable(remove-comments src/RemoveComments.cpp)
target_link_libraries(remove-comments PRIVATE
    clang_tooling_config
)

add_executable(remove-non-preprocessor src/RemoveNonPreprocessor.cpp)
target_link_libraries(remove-non-preprocessor PRIVATE
    clang_tooling_config
)

add_executable(macro-skeleton src/MacroSkeleton.cpp)
target_link_libraries(macro-skeleton PRIVATE
    clang_tooling_config
)


# Rust

add_custom_target(
    analyzer ALL
    COMMAND CARGO_TARGET_DIR=${CMAKE_SOURCE_DIR}/build/ cargo build
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Rust analyzer binary"
)

# Testing

enable_testing()

add_library(testing_config INTERFACE)
target_compile_definitions(testing_config INTERFACE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO
)

add_executable(TempDir_test tests/TempDir_test.cpp)
target_link_libraries(TempDir_test PRIVATE
    testing_config
)
add_test(
    NAME TempDir_test
    COMMAND TempDir_test
)

add_executable(IncludeTree_test tests/IncludeTree_test.cpp)
target_link_libraries(IncludeTree_test PRIVATE
    testing_config
    clang_tooling_config
)
add_test(
    NAME IncludeTree_test
    COMMAND IncludeTree_test
)

add_executable(SymbolTable_test tests/SymbolTable_test.cpp)
target_link_libraries(SymbolTable_test PRIVATE
    testing_config
    tree_sitter_config
)
add_test(
    NAME SymbolTable_test
    COMMAND SymbolTable_test
)
