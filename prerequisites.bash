#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  Hayroll dependency installer
# ---------------------------------------------------------------------------

set -euo pipefail

ROOT_DIR=${ROOT_DIR:-"${PWD}"} # default to current dir if not overridden
ROOT_DIR="$(cd "${ROOT_DIR}" && pwd -P)"
INSTALL_DIR="${ROOT_DIR}/dependencies"
OS_NAME="$(uname -s)"

# --- third-party folders we expect under $INSTALL_DIR ----------------------------------
THIRD_PARTY_DIRS=(Maki tree-sitter tree-sitter-c_preproc c2rust z3 libmcs)

# --- Git URLs + tags -------------------------------------------------------------------
C2RUST_GIT="https://github.com/immunant/c2rust.git"
C2RUST_TAG="v0.20.0"
MAKI_GIT="https://github.com/UW-HARVEST/Maki.git"
MAKI_TAG="0.1.7"
TS_GIT="https://github.com/tree-sitter/tree-sitter.git"
TS_TAG="v0.25.10"
TSC_PREPROC_GIT="https://github.com/UW-HARVEST/tree-sitter-c_preproc.git"
TSC_PREPROC_TAG="0.1.7"
Z3_GIT="https://github.com/Z3Prover/z3.git"
Z3_VERSION="4.13.4"
Z3_TAG="z3-${Z3_VERSION}"
LIBMCS_GIT="https://gitlab.com/gtd-gmbh/libmcs.git"
LIBMCS_TAG="1.2.0"

# --- Parse arguments --------------------------------------------------------
USE_LATEST=false
SUDO=sudo
LLVM_VERSION=""
LLVM_CONFIG_EXE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --latest)
      USE_LATEST=true
      shift
      ;;
    --no-sudo)
      SUDO=
      shift
      ;;
    --llvm-version)
      if [[ -n "${2:-}" ]]; then
        LLVM_VERSION="$2"
        shift 2
      else
        echo "Error: --llvm-version requires a version number"
        exit 1
      fi
      ;;
    --llvm-version=*)
      LLVM_VERSION="${1#*=}"
      shift
      ;;
    -h | --help)
      echo "Usage: $0 [--latest] [--no-sudo] [--llvm-version VERSION] [-h|--help]"
      echo
      echo "Options:"
      echo "  --latest             Fetch the latest main/HEAD versions of Maki and tree-sitter-c_preproc."
      echo "  --no-sudo            Run the script without using sudo for package installation."
      echo "  --llvm-version VER   Specify LLVM/Clang version to install (default: use system default version)."
      echo "  -h, --help           Show this help message."
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Use -h or --help for usage information."
      exit 1
      ;;
  esac
done

get_core_count() {
  if command -v nproc > /dev/null 2>&1; then
    nproc
  elif command -v sysctl > /dev/null 2>&1; then
    sysctl -n hw.ncpu
  else
    echo 1
  fi
}

CORE_COUNT="$(get_core_count)"

echo "=========================================================="
echo "Hayroll prerequisites installer"
echo "Target root directory : ${INSTALL_DIR}"
if [[ -n "${LLVM_VERSION}" ]]; then
  echo "LLVM/Clang version    : ${LLVM_VERSION}"
else
  echo "LLVM/Clang version    : system default"
fi
echo
echo "The following directories will be created / updated:"
for d in "${THIRD_PARTY_DIRS[@]}"; do
  echo "  - ${INSTALL_DIR}/${d}"
done
echo "=========================================================="

mkdir -p "${INSTALL_DIR}"
cd "${INSTALL_DIR}"

# Create a temporary directory for logs
LOG_DIR=$(mktemp -d /tmp/hayroll-prereq-logs-XXXXXX)
trap 'rm -rf "${LOG_DIR}"' EXIT

git_clone_or_checkout() {
  local dir=$1 url=$2 tag=$3
  if [[ -d "${dir}/.git" ]]; then
    if [[ "${tag}" == "main" ]]; then
      git -C "${dir}" fetch origin main --quiet
      local target_ref="origin/main"
    else
      git -C "${dir}" fetch --tags --quiet
      local target_ref="${tag}"
    fi
    if [[ "$(git -C "${dir}" rev-parse HEAD)" != "$(git -C "${dir}" rev-parse "${target_ref}")" ]]; then
      echo "[*] ${dir} exists – syncing to latest ${target_ref}"
      git -C "${dir}" reset --hard "${target_ref}" --quiet
    fi
  elif [[ -d "${dir}" ]]; then
    echo "[*] ${dir} already present (vendored), skipping clone"
  else
    echo "[*] Cloning ${url} into ${dir}"
    git clone --quiet "${url}" "${dir}"
    git -C "${dir}" checkout --quiet "${tag}"
  fi
}

check_version() {
  local pkg=$1
  local min_version=$2
  local max_version=$3
  local installed_version
  installed_version=$(dpkg-query -W -f='${Version}' "$pkg" 2> /dev/null || echo "0")

  if [[ "$installed_version" == "0" ]]; then
    echo "Warning: $pkg is not installed."
    return
  fi

  # Extract major version number for comparison, handling Debian epoch format
  local installed_major
  # Remove epoch (e.g., "1:" from "1:17.0.6-9ubuntu1") and extract major version
  local version_without_epoch="${installed_version#*:}"
  installed_major=$(echo "$version_without_epoch" | grep -oE '^[0-9]+' || echo "0")

  if dpkg --compare-versions "$installed_major" lt "$min_version" || dpkg --compare-versions "$installed_major" gt "$max_version"; then
    echo "Warning: $pkg version in [$min_version, $max_version] is recommended. Installed version: $installed_version"
  fi
}

run_quiet() {
  local log="$LOG_DIR/$1"
  shift
  echo "Running command: $* > $log"
  if ! "$@" > "$log" 2>&1; then
    echo "Error: Command failed: $*"
    echo "========== Output from $log =========="
    cat "$log"
    echo "======================================"
    exit 1
  fi
}

ensure_command() {
  local cmd=$1
  if ! command -v "${cmd}" > /dev/null 2>&1; then
    echo "Error: required command '${cmd}' was not found."
    exit 1
  fi
}

ensure_uv() {
  if command -v uv > /dev/null 2>&1; then
    echo "[*] uv already installed, version: $(uv --version | head -n 1)"
    return
  fi

  echo "[*] Installing uv via standalone installer"
  if command -v curl > /dev/null 2>&1; then
    run_quiet uv-install.log bash -c 'curl -LsSf https://astral.sh/uv/install.sh | sh'
  elif command -v wget > /dev/null 2>&1; then
    run_quiet uv-install.log bash -c 'wget -qO- https://astral.sh/uv/install.sh | sh'
  else
    echo "Error: neither curl nor wget was found. Please install one of them and re-run."
    exit 1
  fi

  local uv_bin_dir="${HOME}/.local/bin"
  if [[ ":${PATH}:" != *":${uv_bin_dir}:"* ]]; then
    export PATH="${uv_bin_dir}:${PATH}"
  fi

  if ! command -v uv > /dev/null 2>&1; then
    echo "Error: uv installation failed. Ensure ${uv_bin_dir} is in your PATH."
    exit 1
  fi

  echo "[*] uv installed successfully"
}

if [[ "${OS_NAME}" == "Darwin" ]]; then
  ensure_command brew

  if [[ -z "${LLVM_VERSION}" ]]; then
    LLVM_VERSION="19"
  fi
  LLVM_FORMULA="llvm@${LLVM_VERSION}"
  echo "[*] Installing system packages via Homebrew"
  run_quiet brew-install.log brew install \
    cmake ninja pkg-config \
    "${LLVM_FORMULA}" boost spdlog z3 bear \
    autoconf automake libtool tree-sitter gnu-getopt

  LLVM_PREFIX="$(brew --prefix "${LLVM_FORMULA}")"
  BREW_PREFIX="$(brew --prefix)"
  Z3_PREFIX="$(brew --prefix z3)"
  BOOST_PREFIX="$(brew --prefix boost)"
  SPDLOG_PREFIX="$(brew --prefix spdlog)"
  GNU_GETOPT_PREFIX="$(brew --prefix gnu-getopt)"

  mkdir -p "${HOME}/.local/bin"
  ln -sf "${LLVM_PREFIX}/bin/clang" "${HOME}/.local/bin/clang-${LLVM_VERSION}"

  export PATH="${GNU_GETOPT_PREFIX}/bin:${HOME}/.local/bin:${LLVM_PREFIX}/bin:${HOME}/.cargo/bin:${PATH}"
  export CMAKE_PREFIX_PATH="${LLVM_PREFIX};${BREW_PREFIX};${Z3_PREFIX};${BOOST_PREFIX};${SPDLOG_PREFIX}"
  LLVM_CONFIG_EXE="${LLVM_PREFIX}/bin/llvm-config"
else
  # Generate LLVM package names based on whether version is specified
  if [[ -n "${LLVM_VERSION}" ]]; then
    CLANG_PKG="clang-${LLVM_VERSION}"
    LIBCLANG_PKG="libclang-${LLVM_VERSION}-dev"
    LLVM_PKG="llvm-${LLVM_VERSION}"
    LLVM_DEV_PKG="llvm-${LLVM_VERSION}-dev"
    LLVM_CONFIG_EXE="llvm-config-${LLVM_VERSION}"
  else
    CLANG_PKG="clang"
    LIBCLANG_PKG="libclang-dev"
    LLVM_PKG="llvm"
    LLVM_DEV_PKG="llvm-dev"
    LLVM_CONFIG_EXE="llvm-config"
  fi

  apt_packages="\
    build-essential git cmake ninja-build pkg-config python3 \
    libspdlog-dev libboost-stacktrace-dev \
    ${CLANG_PKG} ${LIBCLANG_PKG} ${LLVM_PKG} ${LLVM_DEV_PKG} \
    curl autoconf automake libtool bear"

  need_apt_install=no
  # shellcheck disable=SC2086
  for apt_package in ${apt_packages}; do
    if ! /usr/bin/dpkg-query --show "$apt_package" > /dev/null 2>&1; then
      need_apt_install=yes
    fi
  done

  if [[ "${need_apt_install}" == "yes" ]]; then
    echo "[*] Installing system packages via apt"
    run_quiet apt-get.log ${SUDO} apt-get update
    # shellcheck disable=SC2086
    DEBIAN_FRONTEND=noninteractive run_quiet apt-install.log ${SUDO} apt-get install -y --no-install-recommends ${apt_packages}
  fi

  # Check versions of installed LLVM packages
  if [[ -n "${LLVM_VERSION}" ]]; then
    check_version "${CLANG_PKG}" 17 19
    check_version "${LIBCLANG_PKG}" 17 19
    check_version "${LLVM_PKG}" 17 19
    check_version "${LLVM_DEV_PKG}" 17 19
  else
    check_version clang 17 19
    check_version libclang-dev 17 19
    check_version llvm 17 19
    check_version llvm-dev 17 19
  fi
fi

# --- Rust tool-chain (for c2rust & Maki) -------------------------------------
if ! command -v rustc > /dev/null 2>&1; then
  echo "Error: rustc (Rust tool-chain) not found."
  echo "Please install Rust by referring to https://www.rust-lang.org/tools/install"
  echo "Then restart this script."
  exit 1
else
  echo "[*] rustc found, version: $(rustc --version)"
  rust_version=$(rustc --version | awk '{print $2}')
  rust_major=${rust_version%%.*}
  rust_minor=$(echo "${rust_version}" | cut -d. -f2)
  if [[ ${rust_major} -lt 1 || (${rust_major} -eq 1 && ${rust_minor} -lt 84) ]]; then
    echo "Error: Rust 1.84+ required."
    echo "Please run: rustup update stable"
    exit 1
  fi
fi

# --- C2Rust ------------------------------------------------------------------
if ! command -v c2rust > /dev/null 2>&1; then
  echo "[*] Installing c2rust ${C2RUST_TAG}"
  export LLVM_CONFIG_PATH="${LLVM_CONFIG_EXE}"
  run_quiet c2rust-install.log cargo install --git "${C2RUST_GIT}" --tag "${C2RUST_TAG}" --locked c2rust
else
  echo "[*] c2rust already installed, version: $(c2rust --version)"
fi

# --- Z3 ----------------------------------------------------------------------
if [[ "${OS_NAME}" == "Darwin" ]]; then
  echo "[*] Using Homebrew z3: $(brew --prefix z3)"
else
  # z3 takes forever to build, so install through z3-solver, the Python wrapper,
  # which is published by z3 for each release.
  ensure_uv
  if uv tool list 2> /dev/null | grep -q "z3-solver ${Z3_VERSION}"; then
    echo "[*] z3-solver ${Z3_VERSION} already installed via uv"
  else
    run_quiet z3-solver-install.log uv tool install --force z3-solver@${Z3_VERSION}
  fi
  git_clone_or_checkout "z3" "${Z3_GIT}" "${Z3_TAG}"
  pushd z3 > /dev/null
  echo "[*] Installing Z3 with z3-solver prebuilt"
  mkdir -p build && cd build
  run_quiet z3-cmake.log cmake -DCMAKE_BUILD_TYPE=Release -DZ3_BUILD_PYTHON_BINDINGS=OFF ..
  # Copy `libz3.so` and `z3` from `z3-solver` to `build/` so that installation works.
  ln -sf "$(uv tool dir)"/z3-solver/lib/python*/site-packages/z3/lib/libz3.so .
  ln -sf libz3.so "libz3.so.$(echo "${Z3_VERSION}" | awk -F. '{print $1 "." $2}')" # ${major}.${minor}
  ln -sf libz3.so "libz3.so.${Z3_VERSION}".0                                       # ${major}.${minor}.${patch}.0
  ln -sf "$(uv tool dir)/z3-solver/bin/z3" .
  run_quiet z3-install.log ${SUDO} cmake --install .
  popd > /dev/null
fi

# --- tree-sitter core --------------------------------------------------------
git_clone_or_checkout "tree-sitter" "${TS_GIT}" "${TS_TAG}"
echo "[*] Building tree-sitter core"
run_quiet tree-sitter-make.log make -C tree-sitter -j"${CORE_COUNT}"

# --- tree-sitter-c_preproc ---------------------------------------------------
if [[ "${USE_LATEST}" == true ]]; then
  echo "[*] Fetching latest tree-sitter-c_preproc (main/HEAD)"
  git_clone_or_checkout "tree-sitter-c_preproc" "${TSC_PREPROC_GIT}" "main"
else
  git_clone_or_checkout "tree-sitter-c_preproc" "${TSC_PREPROC_GIT}" "${TSC_PREPROC_TAG}"
fi
echo "[*] Building tree-sitter-c_preproc"
run_quiet tsc-preproc-make.log make -C tree-sitter-c_preproc -j"${CORE_COUNT}"

# --- Maki --------------------------------------------------------------------
if [[ "${USE_LATEST}" == true ]]; then
  echo "[*] Fetching latest Maki (main/HEAD)"
  git_clone_or_checkout "Maki" "${MAKI_GIT}" "main"
else
  git_clone_or_checkout "Maki" "${MAKI_GIT}" "${MAKI_TAG}"
fi
pushd Maki > /dev/null
echo "[*] Building Maki"
if [[ "${OS_NAME}" == "Darwin" ]]; then
  # Older Maki revisions miss this include and fail with Apple libc++.
  if ! grep -q '^#include <sstream>$' src/Cpp2CASTConsumer.hh; then
    run_quiet maki-patch.log sed -i.bak '/^#include "json.hpp"$/a\
\
#include <sstream>
' src/Cpp2CASTConsumer.hh
  fi
fi
mkdir -p build && cd build
if command -v ninja > /dev/null 2>&1; then
  run_quiet maki-cmake.log cmake -G Ninja ..
  run_quiet maki-build.log cmake --build . --parallel "${CORE_COUNT}"
else
  run_quiet maki-cmake.log cmake ..
  run_quiet maki-make.log make -j"${CORE_COUNT}"
fi
if [[ "${OS_NAME}" == "Darwin" && -f lib/libcpp2c.dylib ]]; then
  ln -sf libcpp2c.dylib lib/libcpp2c.so
fi
popd > /dev/null

# --- LibmCS ------------------------------------------------------------------
git_clone_or_checkout "libmcs" "${LIBMCS_GIT}" "${LIBMCS_TAG}"
pushd libmcs > /dev/null
if [[ ! -f lib/libmcs.a && ! -f build/libmcs.a ]]; then
  echo "[*] Building LibmCS"
  run_quiet libmcs-configure.log ./configure \
    --cross-compile="" \
    --compilation-flags="" \
    --disable-denormal-handling \
    --disable-long-double-procedures \
    --disable-complex-procedures \
    --little-endian
  run_quiet libmcs-make.log make -j"${CORE_COUNT}"
fi
popd > /dev/null

# Check if ~/.cargo/bin is in PATH
echo "[*] Checking if \$HOME/.cargo/bin is in PATH"
if ! echo "$PATH" | grep -q "$HOME/.cargo/bin"; then
  echo "=========================================================="
  echo "Warning: \$HOME/.cargo/bin is not in your PATH."
  echo "Please add the following line to your shell configuration file (e.g., ~/.bashrc or ~/.zshrc):"
  echo "export PATH=\"\$HOME/.cargo/bin:\$PATH\""
  echo "=========================================================="
fi

if [[ "${OS_NAME}" == "Darwin" ]]; then
  echo "=========================================================="
  echo "macOS notes"
  echo "This script configured the environment with:"
  echo "  PATH includes: ~/.local/bin, Homebrew llvm@${LLVM_VERSION}/bin, gnu-getopt/bin, ~/.cargo/bin"
  echo "  CMAKE_PREFIX_PATH includes Homebrew llvm, z3, boost, and spdlog prefixes"
  echo "If your shell does not inherit these, run build with ./build.bash from this repo."
fi

echo "=========================================================="
echo "All prerequisites installed."
echo "=========================================================="
