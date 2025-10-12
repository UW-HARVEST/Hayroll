cmake_minimum_required(VERSION 3.10)
project(Hayroll LANGUAGES C CXX)

# C++
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13")
        message(FATAL_ERROR "GCC version must be 13 or above. Current version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
endif()
# You may also use clang, but make sure you link to libstdc++ 3.4.32 or above

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -gdwarf-2")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Common config
link_libraries(
    spdlog::spdlog
    Boost::stacktrace_backtrace
    dl
)
add_compile_definitions(
    BOOST_STACKTRACE_USE_BACKTRACE
    BOOST_STACKTRACE_LINK
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

# For backtraces
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

# Clang executable

find_package(LLVM REQUIRED CONFIG)
if(LLVM_PACKAGE_VERSION VERSION_LESS 17.0)
message(FATAL_ERROR "Found LLVM ${LLVM_PACKAGE_VERSION}, but need at least LLVM 17.0")
endif()
if(LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL 20.0)
message(WARNING "Found LLVM ${LLVM_PACKAGE_VERSION}, but cpp2c has not been tested with LLVM 20.0 or greater")
endif()
find_package(Clang REQUIRED CONFIG)
set(CLANG_VERSION "${LLVM_VERSION_MAJOR}")

set(CLANG_EXE "${LLVM_TOOLS_BINARY_DIR}/clang-${CLANG_VERSION}")
if(NOT EXISTS "${CLANG_EXE}")
  message(ERROR "Clang executable not found at ${CLANG_EXE}")
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "CLANG_EXE: ${CLANG_EXE}")

# Z3
find_package(Z3
    REQUIRED
    CONFIG
)

message(STATUS "Z3_VERSION: ${Z3_VERSION}")
message(STATUS "Z3_DIR: ${Z3_DIR}")
message(STATUS "Z3_CXX_INCLUDE_DIRS: ${Z3_CXX_INCLUDE_DIRS}")
message(STATUS "Z3_LIBRARIES: ${Z3_LIBRARIES}")

# c2rust
find_program(C2RUST_EXE c2rust REQUIRED)
message(STATUS "C2RUST_EXE: ${C2RUST_EXE}")

# spdlog
find_package(spdlog REQUIRED)
message(STATUS "spdlog_FOUND: ${spdlog_FOUND}")

# Boost stacktrace
find_package(Boost 1.65 REQUIRED CONFIG COMPONENTS stacktrace_backtrace)
include_directories(${Boost_INCLUDE_DIRS})
message(STATUS "Boost stacktrace_backtrace found: ${Boost_STACKTRACE_BACKTRACE_FOUND}")

# tree-sitter
set(TREE_SITTER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tree-sitter")
set(TREE_SITTER_C_PREPROC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tree-sitter-c_preproc")
set(TREE_SITTER_INCLUDE_DIR "${TREE_SITTER_DIR}/lib/include")
set(TREE_SITTER_C_PREPROC_INCLUDE_DIR "${TREE_SITTER_C_PREPROC_DIR}/bindings/c")

message(STATUS "TREE_SITTER_DIR: ${TREE_SITTER_DIR}")
message(STATUS "TREE_SITTER_C_PREPROC_DIR: ${TREE_SITTER_C_PREPROC_DIR}")

# Maki dir
set(MAKI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Maki")
if(NOT EXISTS "${MAKI_DIR}/evaluation/analyze_macro_invocations_in_program.py")
    message(FATAL_ERROR "Maki analysis script not found at ${MAKI_DIR}/evaluation/analyze_macro_invocations_in_program.py")
endif()
if(NOT EXISTS "${MAKI_DIR}/build/lib/libcpp2c.so")
    message(FATAL_ERROR "Maki cpp2c.so library not found at ${MAKI_DIR}/build/lib/libcpp2c.so")
endif()
message(STATUS "MAKI_DIR: ${MAKI_DIR}")

# libmcs dir
set(LIBMCS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libmcs")

add_library(hayroll_exe_config INTERFACE)
target_compile_definitions(hayroll_exe_config INTERFACE
    HAYROLL_REAPER_EXE="${CMAKE_SOURCE_DIR}/reaper"
    HAYROLL_MERGER_EXE="${CMAKE_SOURCE_DIR}/merger"
)

add_library(external_exe_config INTERFACE)
target_compile_definitions(external_exe_config INTERFACE
    C2RUST_EXE="${C2RUST_EXE}"
    CLANG_EXE="${CLANG_EXE}"
    MAKI_DIR="${MAKI_DIR}"
    LIBMCS_DIR="${LIBMCS_DIR}"
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

# # AddressSanitizer
# add_compile_options(
#     "$<$<CONFIG:Debug>:-fsanitize=address;-fno-omit-frame-pointer>"
# )
# add_link_options(
#     "$<$<CONFIG:Debug>:-fsanitize=address>"
# )

include_directories(include)
include_directories(src)

add_executable(hayroll src/HayrollCLI.cpp)
target_link_libraries(hayroll PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    tree_sitter_config
)

# Rust

add_custom_target(
    Rust ALL
    COMMAND CARGO_TARGET_DIR=${CMAKE_SOURCE_DIR}/build/ cargo build
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Rust binaries"
)

# Testing

enable_testing()

add_library(testing_config INTERFACE)
target_compile_definitions(testing_config INTERFACE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
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

add_executable(Pipeline_test tests/Pipeline_test.cpp)
target_link_libraries(Pipeline_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME Pipeline_test
    COMMAND Pipeline_test
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

add_executable(MakiWrapper_test tests/MakiWrapper_test.cpp)
target_link_libraries(MakiWrapper_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME MakiWrapper_test
    COMMAND MakiWrapper_test
)

add_executable(CompileCommand_test tests/CompileCommand_test.cpp)
target_link_libraries(CompileCommand_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME CompileCommand_test
    COMMAND CompileCommand_test
)

add_executable(RewriteIncludesWrapper_test tests/RewriteIncludesWrapper_test.cpp)
target_link_libraries(RewriteIncludesWrapper_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME RewriteIncludesWrapper_test
    COMMAND RewriteIncludesWrapper_test
)

add_executable(C2RustWrapper_test tests/C2RustWrapper_test.cpp)
target_link_libraries(C2RustWrapper_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME C2RustWrapper_test
    COMMAND C2RustWrapper_test
)

add_executable(ReaperWrapper_test tests/ReaperWrapper_test.cpp)
target_link_libraries(ReaperWrapper_test PRIVATE
    hayroll_exe_config
    external_exe_config
    z3_config
    testing_config
    tree_sitter_config
)
add_test(
    NAME ReaperWrapper_test
    COMMAND ReaperWrapper_test
)
