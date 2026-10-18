# DpX 0.3 — Multi-FV scan (initial)

The EFI Shell command `DpX.efi -rom-scan -input fs0:\\BIOS.ROM` searches user-provided ROM images for independent, nonoverlapping standard PI FV headers. The scanner streams 64 KiB chunks, checks `_FVH` signature at the correct header offset, validates revision, FFS2/FFS3 GUID, header checksum, block-map geometry and length bounds before parsing each FV. It reports **per-FV ROM offsets and Total/Used/Free/Usage when the complete sequential FFS layout is validated**. False signatures are ignored; nested FVs within a verified parent range are not separately counted.

No SPI controller read, full IFWI descriptor decode, encrypted/guided decompression, nested FV recursion or aggregate with child double-counting is claimed. Unsupported layouts get N/A. `-rom-analyze -input ... -fv-space` retains its prior offset-zero one-FV behavior.

A real QEMU/OVMF Shell CI case now loads `DUAL.ROM` with two valid FV headers at offsets 0x1000 and 0x3000 and asserts that DpX reports two separate complete FV layouts. Still pending: real hardware, proprietary container formats and full IFWI validation.
