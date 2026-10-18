# DpX v0.2 Raw FV File Analyzer

Implementation: Shell command `DpX.efi -rom-analyze -input fs0:\\TEST.FV -fv-space`.

This M2 slice supports one complete standard PI FV at **offset zero**, not an arbitrary IFWI. It performs bounded file reads (max 512 MiB), checks FV Header signature/revision/checksum/block map and optional FV extension bounds, walks FFS file headers (including extended size, state and checksum), validates alignment and the entire erased tail, and returns Total/Used/Free/Usage only when every byte is accounted for.

Non-standard, truncated, corrupt or ambiguous FFS content remains **N/A**. Unrecognized Intel regions, nested FVs, compression and ROM-wide scan remain unsupported. No direct SPI access or flash writes.

Golden fixture tests under `tests/dpx` cover erase polarity 0/1, FFS2/FFS3, PAD, invalid checksum and dirty tail. **These validate synthetic input structure only, not actual execution of DpX.efi**. OVMF Shell functional tests and physical hardware tests are still outstanding.

Previous FV2-only `-fv-space` remains a conservative N/A; this new capability requires `-rom-analyze -input ...`.

See [DpX Plan v1.3](DpX_Development_Plan_v1.3.md), Issue #5 and PR #10.
