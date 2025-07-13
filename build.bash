#!/bin/bash

BUILD_DIR="build"
# BUILD_TYPE="Release"
BUILD_TYPE="Debug"

while [[ "$#" -gt 0 ]]; do
    case $1 in
        -d|--debug) BUILD_TYPE="Debug" ;;
        -c|--clean) CLEAN=false ;;
        -h|--help)
            echo "Usage: ./build.bash [options]"
            echo "Options:"
            echo "  -d, --debug      Build in Debug mode (default: Release)"
            echo "  -c, --clean      Clean the build directory before building"
            echo "  -h, --help       Show this help message"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

echo "Build Directory: ${BUILD_DIR}"
echo "Build Type: ${BUILD_TYPE}"

if [[ "${CLEAN}" == true ]]; then
    echo "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
fi

if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "Creating build directory: ${BUILD_DIR}"
    mkdir -p "${BUILD_DIR}"
fi

cd "${BUILD_DIR}" || exit

echo "Configuring project with CMake..."
cmake .. -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
if [[ $? -ne 0 ]]; then
    echo "CMake configuration failed!"
    exit 1
fi

echo "Building project..."
make -j$(nproc)
if [[ $? -ne 0 ]]; then
    echo "Build failed!"
    exit 1
fi

# Add soft links to binaries, from here (build directory) to the root directory
ln -sf "${BUILD_DIR}/seeder" ../seeder
ln -sf "${BUILD_DIR}/pioneer" ../pioneer
ln -sf "${BUILD_DIR}/pipeline" ../pipeline
# Rust binaries are in debug subdirectory
ln -sf "${BUILD_DIR}/debug/reaper" ../reaper

echo "Build completed"
