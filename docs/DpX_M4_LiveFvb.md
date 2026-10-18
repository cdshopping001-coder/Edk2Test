# DpX v0.9 — On-board FV size accounting through FVB2

A DUT running `DpX.efi` (or `-fv-space`) now tries the **firmware-published FVB2 Protocol** to read raw FV bytes directly, with no ROM image or FPDT file supplied.

- Confirm FVB read status before attempting reads; only call the `Read` method, never write/erase. Translate byte offsets using `GetBlockSize` and LBA ranges, honoring unequal erase blocks.
- Accept only correctly identified FFS2/FFS3 firmware volumes with a valid header checksum, complete block map geometry, sane length and bounded header. Walk physical FFS sizes including PAD/deleted-but-unreclaimed bytes and the trailing erase-state range. `USED + FREE = TOTAL`; otherwise report N/A.
- All operations have fixed bounds (128 MiB per FV, 64 KiB FV header, 256 FVB handles and 4096 block runs). No full FV buffering; only small reads.
- **Only FVB-accessible firmware regions are covered.** BIOS portions reachable only through Intel PCH SPI Controller still require a platform-specific backend. Lack of FVB or protected pages is reported, not fabricated.
- `-fv-list` continues to provide FV2 logical inventory. `-fv-space` now attempts FVB physical accounting, not just FV2 N/A.

**Testing:** CI compiles GCC/VS2022 and starts QEMU/OVMF Shell for the new no-file-codepath. Exact real-board FVB/Flash usage and SPI-region success remain unverified until Citadel/RVP runs. Failure or unsupported data must not be counted as successful byte accounting.
