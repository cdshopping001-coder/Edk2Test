# DpX 0.10 — Detailed FV, Decompression, Nested FFS

**Live test-machine workflow:** `DpX.efi -fv-detail` enumerates logical FV2 files and FVB2-readable raw FV layouts without a supplied ROM or FPDT. The zero-argument survey remains fast and read-only.

- FV includes FVB-relative offset, format GUID, optional FV Name GUID from extension header, physical Total/Used/Free where possible. FVB offsets are **not** SPI offsets.
- FFS records: full Name GUID, type, FV-relative byte offset when raw, stored file size, 8-byte aligned allocation, PAD, DELETED_UNRECLAIMED. Deleted/PAD occupy physical Used, never Free.
- Sections: original type and raw stored section bytes; read-only EFI standard compression via stable202608 UefiDecompressLib, with hard input 4 MiB, decoded 8 MiB, scratch 8 MiB, 128x ratio and 4-level recursion limits.
- Nested FV: EFI_SECTION_FIRMWARE_VOLUME_IMAGE content validates FV header, block map and nested FFS allocation. Child FFS counts are separate from parent FFS and never double-added to physical FV Used/Free.
- A compressed PE32/TE/DEPEX section does not create new FFS entries. Only a valid nested FV has its own FFS records.
- GUID_DEFINED with processing-required flags is logged, but unsupported vendor codecs (LZMA/Brotli/custom) are not guessed; unsupported sections are not considered decoded.
- FV2 logical file view has unknown physical offsets and Used/Free; FVB raw bytes provide truthful physical accounting only when accessible. Live Citadel/RVP raw SPI may remain protected.

Validation: GCC/VS2022 and OVMF Shell tests are required, plus future fixture tests covering actual standard-compressed and nested FV data. No real-board acceptance is claimed.
