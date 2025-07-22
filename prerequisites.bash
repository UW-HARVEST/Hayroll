#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  Hayroll automatic dependency installer
# ---------------------------------------------------------------------------

set -euo pipefail

ROOT_DIR=${ROOT_DIR:-"${PWD}"}            # default to current dir if not overridden
INSTALL_DIR="${ROOT_DIR}/.."              # clone/build here

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

git_clone_or_checkout () {
  local dir=$1 url=$2 tag=$3
  if [[ -d "${dir}/.git" ]]; then
    echo "[*] ${dir} exists – fetching & checking out ${tag}"
    git -C "${dir}" fetch --tags --quiet
  else
    echo "[*] Cloning ${url} into ${dir}"
    git clone --quiet "${url}" "${dir}"
  fi
  git -C "${dir}" checkout --quiet "${tag}"
}

echo "[*] Installing system packages via apt (sudo maybe required)"
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential git cmake ninja-build pkg-config python3 \
  libspdlog-dev libboost-stacktrace-dev \
  clang-14 llvm-14 libclang-14-dev llvm-14-dev curl autoconf automake libtool \

# --- Rust tool-chain (for c2rust & Maki) -------------------------------------
if ! command -v cargo >/dev/null 2>&1; then
  echo "[*] rustup not found – installing stable Rust tool-chain"
  curl https://sh.rustup.rs -sSf | sh -s -- -y
  export PATH="$HOME/.cargo/bin:$PATH"
fi

# --- C2Rust ------------------------------------------------------------------
if ! command -v c2rust >/dev/null 2>&1; then
  echo "[*] Installing c2rust ${C2RUST_TAG}"
  cargo install --git "${C2RUST_GIT}" --tag "${C2RUST_TAG}" --locked c2rust
fi

# --- Z3 ----------------------------------------------------------------------
git_clone_or_checkout "z3" "${Z3_GIT}" "${Z3_TAG}"
pushd z3 >/dev/null
  mkdir -p build && cd build
  cmake -DCMAKE_BUILD_TYPE=Release -DZ3_BUILD_PYTHON_BINDINGS=OFF ..
  make -j"$(nproc)"
  sudo make install
popd >/dev/null

# --- tree-sitter core --------------------------------------------------------
git_clone_or_checkout "tree-sitter" "${TS_GIT}" "${TS_TAG}"
make -C tree-sitter -j"$(nproc)"

# --- tree-sitter-c_preproc ----------------------------------------------------
git_clone_or_checkout "tree-sitter-c_preproc" "${TSC_PREPROC_GIT}" "${TSC_PREPROC_TAG}"
make -C tree-sitter-c_preproc -j"$(nproc)"

# --- Maki --------------------------------------------------------------------
git_clone_or_checkout "Maki" "${MAKI_GIT}" "${MAKI_TAG}"
pushd Maki >/dev/null
  mkdir -p build && cd build
  cmake ..
  make -j"$(nproc)"
popd >/dev/null

### --- Libmcs --------------------------------------------------------------
# Clone + build LibmCS v1.2.0 completely non-interactive.
git_clone_or_checkout "libmcs" "${LIBMCS_GIT}" "${LIBMCS_TAG}"
pushd libmcs >/dev/null
  if [[ ! -f lib/libmcs.a && ! -f build/libmcs.a ]]; then
    echo "[*] Configuring LibmCS (non-interactive)…"
    # Passing an explicit empty string to --cross-compile prevents the script
    # from prompting for a tool-chain path; all other options are disabled to
    # match Hayroll’s requirements.
    ./configure \
        --cross-compile="" \
        --compilation-flags="" \
        --disable-denormal-handling \
        --disable-long-double-procedures \
        --disable-complex-procedures \
        --little-endian
    make -j"$(nproc)"
  fi
popd >/dev/null

echo "=========================================================="
echo "All prerequisites installed."
echo "Add \$HOME/.cargo/bin to your PATH if missing."
echo "=========================================================="
