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
set(CT_LLVM_INCLUDE_DIR "${CT_LLVM_INSTALL_DIR}/include/llvm-17")
if(NOT EXISTS "${CT_LLVM_INCLUDE_DIR}")
message(FATAL_ERROR
    " CT_LLVM_INSTALL_DIR (${CT_LLVM_INCLUDE_DIR}) is invalid.")
endif()

set(CT_LLVM_CMAKE_FILE "${CT_LLVM_INSTALL_DIR}/lib/cmake/clang-17/ClangConfig.cmake")
if(NOT EXISTS "${CT_LLVM_CMAKE_FILE}")
message(FATAL_ERROR
    " CT_LLVM_CMAKE_FILE (${CT_LLVM_CMAKE_FILE}) is invalid.")
endif()

#===============================================================================
# 2. LOAD CLANG CONFIGURATION
#    For more: http://llvm.org/docs/CMake.html#embedding-llvm-in-your-project
#===============================================================================
set(CLANG_VERSION 17)
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
message(STATUS "AWK_EXE: ${AWK_EXE}")

# spdlog
find_package(spdlog REQUIRED)
message(STATUS "spdlog_FOUND: ${spdlog_FOUND}")

# # fmt
# find_package(fmt REQUIRED)

# Boost
cmake_policy(SET CMP0167 NEW)
find_package(Boost 1.65 REQUIRED CONFIG COMPONENTS stacktrace_addr2line)
include_directories(${Boost_INCLUDE_DIRS})

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
    # fmt::fmt
    Boost::stacktrace_addr2line
    dl
)
add_compile_definitions(
    BOOST_STACKTRACE_USE_ADDR2LINE
    BOOST_STACKTRACE_LINK
)

# For backtraces
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

add_library(hayroll_exe_config INTERFACE)
target_compile_definitions(hayroll_exe_config INTERFACE
    HAYROLL_SEEDER_EXE="$<TARGET_FILE:seeder>"
    HAYROLL_PIONEER_EXE="$<TARGET_FILE:pioneer>"
)

add_library(external_exe_config INTERFACE)
target_compile_definitions(external_exe_config INTERFACE
    AWK_EXE="${AWK_EXE}"
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

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13")
        message(FATAL_ERROR "GCC version must be 13 or above. Current version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
endif()
# You may also use clang, but make sure you have libstdc++ 13 or above

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -gdwarf-2")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# # AddressSanitizer
# add_compile_options(
#     "$<$<CONFIG:Debug>:-fsanitize=address;-fno-omit-frame-pointer>"
# )
# add_link_options(
#     "$<$<CONFIG:Debug>:-fsanitize=address>"
# )

include_directories(include)
include_directories(src)

add_executable(seeder src/seeder.cpp)
target_link_libraries(seeder PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    tree_sitter_config
)

add_executable(pioneer src/pioneer.cpp)
target_link_libraries(pioneer PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    tree_sitter_config
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
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
)

add_executable(TempDir_test tests/TempDir_test.cpp)
target_link_libraries(TempDir_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
)
add_test(
    NAME TempDir_test
    COMMAND TempDir_test
)

add_executable(IncludeTree_test tests/IncludeTree_test.cpp)
target_link_libraries(IncludeTree_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME IncludeTree_test
    COMMAND IncludeTree_test
)

add_executable(SymbolTable_test tests/SymbolTable_test.cpp)
target_link_libraries(SymbolTable_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME SymbolTable_test
    COMMAND SymbolTable_test
)

add_executable(ASTBank_test tests/ASTBank_test.cpp)
target_link_libraries(ASTBank_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME ASTBank_test
    COMMAND ASTBank_test
)

add_executable(MacroExpander_test tests/MacroExpander_test.cpp)
target_link_libraries(MacroExpander_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME MacroExpander_test
    COMMAND MacroExpander_test
)

add_executable(SymbolicExecutor_test tests/SymbolicExecutor_test.cpp)
target_link_libraries(SymbolicExecutor_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME SymbolicExecutor_test
    COMMAND SymbolicExecutor_test
)

add_executable(LineMatcher_test tests/LineMatcher_test.cpp)
target_link_libraries(LineMatcher_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME LineMatcher_test
    COMMAND LineMatcher_test
)

add_executable(Seeder_test tests/Seeder_test.cpp)
target_link_libraries(Seeder_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME Seeder_test
    COMMAND Seeder_test
)

add_executable(TextEditor_test tests/TextEditor_test.cpp)
target_link_libraries(TextEditor_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME TextEditor_test
    COMMAND TextEditor_test
)

add_executable(LinemarkerEraser_test tests/LinemarkerEraser_test.cpp)
target_link_libraries(LinemarkerEraser_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME LinemarkerEraser_test
    COMMAND LinemarkerEraser_test
)
