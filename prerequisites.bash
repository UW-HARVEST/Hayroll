#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  Hayroll dependency installer
# ---------------------------------------------------------------------------

set -euo pipefail

ROOT_DIR=${ROOT_DIR:-"${PWD}"}           # default to current dir if not overridden
INSTALL_DIR=$(realpath "${ROOT_DIR}/..") # normalize the path

# --- third-party folders we expect under $INSTALL_DIR ----------------------------------
THIRD_PARTY_DIRS=(Maki tree-sitter tree-sitter-c_preproc c2rust z3 libmcs)

# --- Git URLs + tags -------------------------------------------------------------------
C2RUST_GIT="https://github.com/immunant/c2rust.git"
C2RUST_TAG="v0.19.0"
MAKI_GIT="https://github.com/UW-HARVEST/Maki.git"
MAKI_TAG="0.1.3"
TS_GIT="https://github.com/tree-sitter/tree-sitter.git"
TS_TAG="v0.25.3"
TSC_PREPROC_GIT="https://github.com/UW-HARVEST/tree-sitter-c_preproc.git"
TSC_PREPROC_TAG="0.1.3"
Z3_GIT="https://github.com/Z3Prover/z3.git"
Z3_TAG="z3-4.13.4"
LIBMCS_GIT="https://gitlab.com/gtd-gmbh/libmcs.git"
LIBMCS_TAG="1.2.0"

# --- Parse arguments --------------------------------------------------------
USE_LATEST=false
SUDO=sudo
LLVM_VERSION=""

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

git_clone_or_checkout() {
  local dir=$1 url=$2 tag=$3
  if [[ -d "${dir}/.git" ]]; then
    if [[ "$(git -C "${dir}" rev-parse HEAD)" != "$(git -C "${dir}" rev-parse "${tag}")" ]]; then
      echo "[*] ${dir} exists – fetching & checking out ${tag}"
      git -C "${dir}" fetch --tags --quiet
      if [[ "${tag}" == "main" ]]; then
        git -C "${dir}" fetch origin main --quiet
        git -C "${dir}" reset --hard origin/main --quiet
      else
        git -C "${dir}" checkout --quiet "${tag}"
      fi
    fi
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
  check_version "${CLANG_PKG}" 17 17
  check_version "${LIBCLANG_PKG}" 17 17
  check_version "${LLVM_PKG}" 17 17
  check_version "${LLVM_DEV_PKG}" 17 17
else
  check_version clang 17 17
  check_version libclang-dev 17 17
  check_version llvm 17 17
  check_version llvm-dev 17 17
fi

# --- Rust tool-chain (for c2rust & Maki) -------------------------------------
if ! command -v rustc > /dev/null 2>&1; then
  echo "Error: rustc (Rust tool-chain) not found."
  echo "Please install Rust by referring to https://www.rust-lang.org/tools/install"
  echo "Then restart this script."
  exit 1
else
  echo "[*] rustc found, version: $(rustc --version)"
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
git_clone_or_checkout "z3" "${Z3_GIT}" "${Z3_TAG}"
pushd z3 > /dev/null
echo "[*] Building Z3"
mkdir -p build && cd build
run_quiet z3-cmake.log cmake -DCMAKE_BUILD_TYPE=Release -DZ3_BUILD_PYTHON_BINDINGS=OFF ..
run_quiet z3-make.log make -j"$(nproc)"
run_quiet z3-install.log ${SUDO} make install
popd > /dev/null

# --- tree-sitter core --------------------------------------------------------
git_clone_or_checkout "tree-sitter" "${TS_GIT}" "${TS_TAG}"
echo "[*] Building tree-sitter core"
run_quiet tree-sitter-make.log make -C tree-sitter -j"$(nproc)"

# --- tree-sitter-c_preproc ---------------------------------------------------
if [[ "${USE_LATEST}" == true ]]; then
  echo "[*] Fetching latest tree-sitter-c_preproc (main/HEAD)"
  git_clone_or_checkout "tree-sitter-c_preproc" "${TSC_PREPROC_GIT}" "main"
else
  git_clone_or_checkout "tree-sitter-c_preproc" "${TSC_PREPROC_GIT}" "${TSC_PREPROC_TAG}"
fi
echo "[*] Building tree-sitter-c_preproc"
run_quiet tsc-preproc-make.log make -C tree-sitter-c_preproc -j"$(nproc)"

# --- Maki --------------------------------------------------------------------
if [[ "${USE_LATEST}" == true ]]; then
  echo "[*] Fetching latest Maki (main/HEAD)"
  git_clone_or_checkout "Maki" "${MAKI_GIT}" "main"
else
  git_clone_or_checkout "Maki" "${MAKI_GIT}" "${MAKI_TAG}"
fi
pushd Maki > /dev/null
echo "[*] Building Maki"
mkdir -p build && cd build
run_quiet maki-cmake.log cmake ..
run_quiet maki-make.log make -j"$(nproc)"
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
  run_quiet libmcs-make.log make -j"$(nproc)"
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

echo "=========================================================="
echo "All prerequisites installed."
echo "=========================================================="
