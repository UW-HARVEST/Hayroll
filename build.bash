#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
cd "${SCRIPT_DIR}"

BUILD_DIR="build"
BUILD_TYPE="Debug"   # Default to Debug; use -r/--release for Release
RUST_PROFILE="debug" # Cargo profile directory name
LLVM_VERSION="${LLVM_VERSION:-19}"
OS_NAME="$(uname -s)"
CLEAN=false

get_core_count() {
  if command -v nproc > /dev/null 2>&1; then
    nproc
  elif command -v sysctl > /dev/null 2>&1; then
    sysctl -n hw.ncpu
  else
    echo 1
  fi
}

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

if [[ "${OS_NAME}" == "Darwin" ]] && command -v brew > /dev/null 2>&1; then
  LLVM_PREFIX="$(brew --prefix "llvm@${LLVM_VERSION}" 2> /dev/null || true)"
  if [[ -n "${LLVM_PREFIX}" ]]; then
    BREW_PREFIX="$(brew --prefix)"
    Z3_PREFIX="$(brew --prefix z3)"
    BOOST_PREFIX="$(brew --prefix boost)"
    SPDLOG_PREFIX="$(brew --prefix spdlog)"
    GNU_GETOPT_PREFIX="$(brew --prefix gnu-getopt)"
    mkdir -p "${HOME}/.local/bin"
    ln -sf "${LLVM_PREFIX}/bin/clang" "${HOME}/.local/bin/clang-${LLVM_VERSION}"
    export PATH="${GNU_GETOPT_PREFIX}/bin:${HOME}/.local/bin:${LLVM_PREFIX}/bin:${HOME}/.cargo/bin:${PATH}"
    export CMAKE_PREFIX_PATH="${LLVM_PREFIX};${BREW_PREFIX};${Z3_PREFIX};${BOOST_PREFIX};${SPDLOG_PREFIX}"
  fi
fi

if [[ "${CLEAN}" == true ]]; then
  echo "Cleaning build directory..."
  rm -rf "${BUILD_DIR}"
fi

if command -v ninja > /dev/null 2>&1; then
  CMAKE_GENERATOR_ARGS=(-G Ninja)
else
  CMAKE_GENERATOR_ARGS=()
fi

echo "Configuring project with CMake..."
if ! cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" "${CMAKE_GENERATOR_ARGS[@]}"; then
  echo "CMake configuration failed!"
  exit 1
fi

echo "Building project..."
# Use `cmake --build` so the script works with any CMake generator (Ninja, Unix Makefiles, etc.)
if ! cmake --build "${BUILD_DIR}" --parallel "$(get_core_count)"; then
  echo "Build failed!"
  exit 1
fi

# Add soft links to binaries in the repo root
ln -sf "${BUILD_DIR}/hayroll" hayroll
RUST_BIN_DIR="${BUILD_DIR}/${RUST_PROFILE}"
ln -sf "${RUST_BIN_DIR}/reaper" reaper
ln -sf "${RUST_BIN_DIR}/merger" merger
ln -sf "${RUST_BIN_DIR}/inliner" inliner
ln -sf "${RUST_BIN_DIR}/cleaner" cleaner

echo "Build completed"
