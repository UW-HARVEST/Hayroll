#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  Hayroll automatic dependency installer
# ---------------------------------------------------------------------------

set -euo pipefail

ROOT_DIR=${ROOT_DIR:-"${PWD}"}            # default to current dir if not overridden
INSTALL_DIR=$(realpath "${ROOT_DIR}/..")  # normalize the path

# --- third-party folders we expect under $INSTALL_DIR ----------------------------------
THIRD_PARTY_DIRS=(Maki tree-sitter tree-sitter-c_preproc c2rust z3 libmcs)

# --- Git URLs + tags -------------------------------------------------------------------
C2RUST_GIT="https://github.com/immunant/c2rust.git";
C2RUST_TAG="v0.19.0"
MAKI_GIT="https://github.com/UW-HARVEST/Maki.git";
MAKI_TAG="0.1.1"
TS_GIT="https://github.com/tree-sitter/tree-sitter.git";
TS_TAG="v0.25.3"
TSC_PREPROC_GIT="https://github.com/UW-HARVEST/tree-sitter-c_preproc.git";
TSC_PREPROC_TAG="0.1.1"
Z3_GIT="https://github.com/Z3Prover/z3.git";
Z3_TAG="z3-4.13.4"
LIBMCS_GIT="https://gitlab.com/gtd-gmbh/libmcs.git"
LIBMCS_TAG="1.2.0"

# --- Parse arguments --------------------------------------------------------
USE_LATEST=false
USE_SUDO=true
if [[ $# -gt 0 ]]; then
  case "$1" in
    --latest)
      USE_LATEST=true
      ;;
    --no-sudo)
      USE_SUDO=false
      ;;
    -h|--help)
      echo "Usage: $0 [--latest] [--no-sudo] [-h|--help]"
      echo
      echo "Options:"
      echo "  --latest   Fetch the latest main/HEAD versions of Maki and tree-sitter-c_preproc."
      echo "  --no-sudo  Run the script without using sudo for package installation."
      echo "  -h, --help Show this help message."
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Use -h or --help for usage information."
      exit 1
      ;;
  esac
fi

echo "=========================================================="
echo "Hayroll prerequisites installer"
echo "Target root directory : ${INSTALL_DIR}"
echo
echo "The following directories will be created / updated:"
for d in "${THIRD_PARTY_DIRS[@]}"; do
  echo "  - ${INSTALL_DIR}/${d}"
done
echo "=========================================================="
read -rp "Proceed? [y/N] " yn
[[ "${yn:-N}" =~ ^[Yy]$ ]] || { echo "Aborted."; exit 1; }

mkdir -p "${INSTALL_DIR}"
cd "${INSTALL_DIR}"

# Create a temporary directory for logs
LOG_DIR=$(mktemp -d /tmp/hayroll-prereq-logs-XXXXXX)

git_clone_or_checkout () {
  local dir=$1 url=$2 tag=$3
  if [[ -d "${dir}/.git" ]]; then
    echo "[*] ${dir} exists – fetching & checking out ${tag}"
    git -C "${dir}" fetch --tags --quiet
    if [[ "${tag}" == "main" ]]; then
      git -C "${dir}" fetch origin main --quiet
      git -C "${dir}" reset --hard origin/main --quiet
    else
      git -C "${dir}" checkout --quiet "${tag}"
    fi
  else
    echo "[*] Cloning ${url} into ${dir}"
    git clone --quiet "${url}" "${dir}"
    if [[ "${tag}" == "main" ]]; then
      git -C "${dir}" checkout --quiet main
    else
      git -C "${dir}" checkout --quiet "${tag}"
    fi
  fi
}

check_version() {
  local pkg=$1
  local min_version=$2
  local installed_version
  installed_version=$(dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null || echo "0")
  if dpkg --compare-versions "$installed_version" lt "$min_version"; then
    echo "Error: $pkg version >= $min_version is required. Installed version: $installed_version"
    exit 1
  fi
}

run_quiet() {
  local log="$LOG_DIR/$1"
  shift
  if ! "$@" >"$log" 2>&1; then
    echo "Error: Command failed: $*"
    echo "========== Output from $log =========="
    cat "$log"
    echo "======================================"
    exit 1
  fi
}

echo "[*] Installing system packages via apt"
run_quiet apt-get.log ${USE_SUDO:+sudo} apt-get update
run_quiet apt-install.log ${USE_SUDO:+sudo} apt-get install -y --no-install-recommends \
  build-essential git cmake ninja-build pkg-config python3 \
  libspdlog-dev libboost-stacktrace-dev \
  clang libclang-dev llvm llvm-dev \
  curl autoconf automake libtool bear

check_version clang 17
check_version libclang-dev 17
check_version llvm 17
check_version llvm-dev 17

# --- Rust tool-chain (for c2rust & Maki) -------------------------------------
if ! command -v cargo >/dev/null 2>&1; then
  echo "[*] rustup not found – installing stable Rust tool-chain"
  run_quiet rustup-install.log curl https://sh.rustup.rs -sSf | sh -s -- -y
  export PATH="$HOME/.cargo/bin:$PATH"
fi

# --- C2Rust ------------------------------------------------------------------
if ! command -v c2rust >/dev/null 2>&1; then
  echo "[*] Installing c2rust ${C2RUST_TAG}"
  run_quiet c2rust-install.log cargo install --git "${C2RUST_GIT}" --tag "${C2RUST_TAG}" --locked c2rust
fi

# --- Z3 ----------------------------------------------------------------------
git_clone_or_checkout "z3" "${Z3_GIT}" "${Z3_TAG}"
pushd z3 >/dev/null
  mkdir -p build && cd build
  run_quiet z3-cmake.log cmake -DCMAKE_BUILD_TYPE=Release -DZ3_BUILD_PYTHON_BINDINGS=OFF ..
  run_quiet z3-make.log make -j"$(nproc)"
  run_quiet z3-install.log ${USE_SUDO:+sudo} make install
popd >/dev/null

# --- tree-sitter core --------------------------------------------------------
git_clone_or_checkout "tree-sitter" "${TS_GIT}" "${TS_TAG}"
run_quiet tree-sitter-make.log make -C tree-sitter -j"$(nproc)"

# --- tree-sitter-c_preproc ---------------------------------------------------
if [[ "${USE_LATEST}" == true ]]; then
  echo "[*] Fetching latest tree-sitter-c_preproc (main/HEAD)"
  git_clone_or_checkout "tree-sitter-c_preproc" "${TSC_PREPROC_GIT}" "main"
else
  git_clone_or_checkout "tree-sitter-c_preproc" "${TSC_PREPROC_GIT}" "${TSC_PREPROC_TAG}"
fi
run_quiet tsc-preproc-make.log make -C tree-sitter-c_preproc -j"$(nproc)"

# --- Maki --------------------------------------------------------------------
if [[ "${USE_LATEST}" == true ]]; then
  echo "[*] Fetching latest Maki (main/HEAD)"
  git_clone_or_checkout "Maki" "${MAKI_GIT}" "main"
else
  git_clone_or_checkout "Maki" "${MAKI_GIT}" "${MAKI_TAG}"
fi
pushd Maki >/dev/null
  mkdir -p build && cd build
  run_quiet maki-cmake.log cmake ..
  run_quiet maki-make.log make -j"$(nproc)"
popd >/dev/null

# --- LibmCS ------------------------------------------------------------------
git_clone_or_checkout "libmcs" "${LIBMCS_GIT}" "${LIBMCS_TAG}"
pushd libmcs >/dev/null
  if [[ ! -f lib/libmcs.a && ! -f build/libmcs.a ]]; then
    echo "[*] Configuring LibmCS (non-interactive)..."
    run_quiet libmcs-configure.log ./configure \
        --cross-compile="" \
        --compilation-flags="" \
        --disable-denormal-handling \
        --disable-long-double-procedures \
        --disable-complex-procedures \
        --little-endian
    run_quiet libmcs-make.log make -j"$(nproc)"
  fi
popd >/dev/null

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
