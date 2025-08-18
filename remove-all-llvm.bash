#!/usr/bin/env bash
# Uninstall all clang/llvm versions (1-100) and related packages
set -euo pipefail

SUDO="${SUDO:-sudo}"
export DEBIAN_FRONTEND=noninteractive

"$SUDO" apt update
"$SUDO" apt remove -y \
  clang \
  llvm \
  llvm-dev \
  llvm-runtime:amd64 \
  || true
for ver in {1..100}; do
  "$SUDO" apt remove -y \
    clang-"${ver}" clang-tools-"${ver}" clangd-"${ver}" lld-"${ver}" \
    libclang-common-"${ver}"-dev libclang-common-"${ver}"-dev:amd64 libclang-cpp"${ver}" \
    llvm-"${ver}" llvm-"${ver}"-dev llvm-"${ver}"-linker-tools llvm-"${ver}"-runtime llvm-"${ver}"-tools \
    || true
done
