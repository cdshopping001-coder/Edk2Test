#!/usr/bin/env bash
set -Eeuo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-DEBUG}"
case "$CONFIG" in DEBUG|RELEASE) ;; *) echo "Usage: $0 [DEBUG|RELEASE]" >&2; exit 2;; esac

for tool in git make gcc g++ nasm iasl python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "ERROR: missing $tool; see README.md for prerequisites." >&2
    exit 1
  fi
done

git -C "$REPO_ROOT" submodule update --init --recursive
python3 -m venv "$REPO_ROOT/.venv"
# shellcheck disable=SC1091
source "$REPO_ROOT/.venv/bin/activate"
python -m pip install --disable-pip-version-check -r "$REPO_ROOT/edk2/pip-requirements.txt"

mkdir -p "$REPO_ROOT/artifacts"
cd "$REPO_ROOT/edk2"
make -C BaseTools -j "$(nproc)"
export PYTHON_COMMAND="$(command -v python)"
# shellcheck disable=SC1091
source ./edksetup.sh
build -p OvmfPkg/OvmfPkgX64.dsc -a X64 -t GCC -b "$CONFIG" -n "$(nproc)" \
  -y "$REPO_ROOT/artifacts/linux-${CONFIG}-build-report.txt" -Y PCD -Y FLASH

IMAGE="$PWD/Build/OvmfX64/${CONFIG}_GCC/FV/OVMF.fd"
if [[ ! -f "$IMAGE" ]]; then
  echo "ERROR: build returned success but OVMF.fd is missing: $IMAGE" >&2
  exit 1
fi
echo "SUCCESS: $IMAGE"
