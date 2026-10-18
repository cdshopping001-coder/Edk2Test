#!/usr/bin/env bash
# DpX standalone X64 build. No OVMF firmware rebuild.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-RELEASE}"
case "$CONFIG" in DEBUG|RELEASE) ;; *) echo "Usage: $0 [DEBUG|RELEASE]" >&2; exit 2 ;; esac
for tool in git gcc make nasm python3; do
  command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }
done
git -C "$ROOT" submodule update --init edk2
git -C "$ROOT/edk2" submodule update --init BaseTools/Source/C/BrotliCompress/brotli
if [[ ! -f "$ROOT/.venv/bin/activate" ]]; then python3 -m venv "$ROOT/.venv"; fi
source "$ROOT/.venv/bin/activate"
python -m pip install --disable-pip-version-check -r "$ROOT/scripts/pip-requirements-ovmf.txt"
cd "$ROOT/edk2"
make -C BaseTools -j "$(nproc)"
export WORKSPACE="$ROOT/edk2"
export PACKAGES_PATH="$ROOT${PACKAGES_PATH:+:$PACKAGES_PATH}"
export PYTHON_COMMAND="$(command -v python)"
set +u
source ./edksetup.sh --reconfig
set -u
build -p CommonToolsPkg/CommonToolsPkg.dsc -a X64 -t GCC -b "$CONFIG" -n "$(nproc)"
OUTPUT="$(find "$WORKSPACE/Build/DpX" -type f -name 'DpX.efi' -print -quit)"
if [[ -z "$OUTPUT" || ! -s "$OUTPUT" ]]; then echo "DpX.efi not found or empty" >&2; exit 1; fi
mkdir -p "$ROOT/artifacts/dpx"
cp "$OUTPUT" "$ROOT/artifacts/dpx/DpX-GCC-X64-$CONFIG.efi"
sha256sum "$ROOT/artifacts/dpx/DpX-GCC-X64-$CONFIG.efi"
echo "SUCCESS: $ROOT/artifacts/dpx/DpX-GCC-X64-$CONFIG.efi"
