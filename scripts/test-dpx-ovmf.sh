#!/usr/bin/env bash
# Real QEMU/OVMF Shell integration test of DpX.efi (read-only)
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EFI="${1:-$ROOT/artifacts/dpx/DpX-GCC-X64-RELEASE.efi}"
[[ -s "$EFI" ]] || { echo "DpX EFI binary missing: $EFI" >&2; exit 2; }
for exe in qemu-system-x86_64 mkfs.vfat mcopy timeout python3; do command -v "$exe" >/dev/null || exit 2; done
CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.fd
[[ -s "$CODE" && -s "$VARS" ]] || { echo "Missing OVMF 4M firmware pair" >&2; exit 2; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

python3 "$ROOT/tests/dpx/generate_ovmf_fixtures.py" "$TMP"
cp "$EFI" "$TMP/DpX.efi"
cat >"$TMP/startup.nsh" <<'NSH'
@echo -off
fs0:
echo DPX_SMOKE_BEGIN > fs0:\dpx-result.txt
DpX.efi -version >> fs0:\dpx-result.txt
DpX.efi >> fs0:\dpx-result.txt
DpX.efi -summary >> fs0:\dpx-result.txt
DpX.efi -rom-analyze -input fs0:\VALID.FV -fv-space >> fs0:\dpx-result.txt
DpX.efi -rom-analyze -input fs0:\BAD.FV -fv-space >> fs0:\dpx-result.txt
DpX.efi -rom-scan -input fs0:\DUAL.ROM >> fs0:\dpx-result.txt
DpX.efi -perf-parse -input fs0:\VALID.FBPT >> fs0:\dpx-result.txt
DpX.efi -perf-parse -input fs0:\BAD.FBPT >> fs0:\dpx-result.txt
DpX.efi -perf-parse -input fs0:\SHELL.FBPT >> fs0:\dpx-result.txt
DpX.efi -perf-fpdt -input fs0:\VALID.FPDT >> fs0:\dpx-result.txt
DpX.efi -perf-fpdt -input fs0:\BAD.FPDT >> fs0:\dpx-result.txt
DpX.efi -perf-live >> fs0:\dpx-result.txt
DpX.efi -perf-live -boot >> fs0:\dpx-result.txt
DpX.efi -perf-live -pointers >> fs0:\dpx-result.txt
echo DPX_SMOKE_DONE >> fs0:\dpx-result.txt
reset -s
NSH
dd if=/dev/zero of="$TMP/disk.img" bs=1M count=24 status=none
mkfs.vfat -F 16 -n DPXTEST "$TMP/disk.img" >/dev/null
for file in startup.nsh DpX.efi VALID.FV BAD.FV DUAL.ROM VALID.FBPT BAD.FBPT SHELL.FBPT VALID.FPDT BAD.FPDT; do mcopy -i "$TMP/disk.img" "$TMP/$file" "::/$file"; done
cp "$VARS" "$TMP/vars.fd"
set +e
timeout --kill-after=8s 110s qemu-system-x86_64 \
  -machine q35,accel=tcg -cpu qemu64 -m 512 -smp 1 \
  -display none -serial "file:$TMP/serial.log" -monitor none -net none \
  -drive "if=pflash,unit=0,format=raw,readonly=on,file=$CODE" \
  -drive "if=pflash,unit=1,format=raw,file=$TMP/vars.fd" \
  -drive "if=ide,format=raw,media=disk,file=$TMP/disk.img"
QEMU_STATUS=$?
set -e
mcopy -i "$TMP/disk.img" "::/dpx-result.txt" "$TMP/results.txt" || {
  echo "No UEFI Shell results (qemu=$QEMU_STATUS)" >&2
  tail -n 60 "$TMP/serial.log" >&2 || true
  exit 1
}
cat "$TMP/results.txt"
python3 "$ROOT/tests/dpx/verify_ovmf_result.py" "$TMP/results.txt"
echo "PASS: Real OVMF Shell executed DpX FV parser"
