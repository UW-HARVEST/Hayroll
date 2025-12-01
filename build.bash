#!/bin/bash

BUILD_DIR="build"
BUILD_TYPE="Debug" # Default to Debug; use -r/--release for Release
RUST_PROFILE="debug" # Cargo profile directory name

while [[ "$#" -gt 0 ]]; do
  case $1 in
    -r | --release)
      BUILD_TYPE="Release"
      RUST_PROFILE="release"
      ;;
    -c | --clean) CLEAN=true ;;
    -h | --help)
      echo "Usage: ./build.bash [options]"
      echo "Options:"
      echo "  -r, --release    Build in Release mode"
      echo "  -c, --clean      Clean the build directory before building"
      echo "  -h, --help       Show this help message"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
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
if ! cmake .. -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"; then
  echo "CMake configuration failed!"
  exit 1
fi

echo "Building project..."
# Use `cmake --build` so the script works with any CMake generator (Ninja, Unix Makefiles, etc.)
if ! cmake --build . -- -j"$(nproc)"; then
  echo "Build failed!"
  exit 1
fi

# Add soft links to binaries, from here (build directory) to the root directory
ln -sf "${BUILD_DIR}/hayroll" ../hayroll
RUST_BIN_DIR="${BUILD_DIR}/${RUST_PROFILE}"
ln -sf "${RUST_BIN_DIR}/reaper" ../reaper
ln -sf "${RUST_BIN_DIR}/merger" ../merger
ln -sf "${RUST_BIN_DIR}/inliner" ../inliner
ln -sf "${RUST_BIN_DIR}/cleaner" ../cleaner

echo "Build completed"
