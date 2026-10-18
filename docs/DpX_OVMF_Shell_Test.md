# OVMF Shell execution test

The M2 raw FV parser has a separate real guest-invocation gate:
`bash scripts/test-dpx-ovmf.sh artifacts/dpx/DpX-GCC-X64-RELEASE.efi`.

Ubuntu 24.04 with QEMU, OVMF (4 MiB matching CODE/VARS), dosfstools, mtools is required. The script builds a temporary FAT image, copies DpX.efi and two synthetic FV files, boots QEMU with the prebuilt OVMF Shell, runs a startup.nsh script invoking the **real EFI program**, then checks a result file written by the Shell. Known-good 4096-byte fixture must report Used=136 and Free=3960. Bad checksum must return N/A, not success.

This exercises the actual executable and Shell filesystem access, in contrast to Python-only fixture geometry tests. It does not prove runtime on Dell Citadel / Intel RVP.
