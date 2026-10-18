# Real QEMU/OVMF deep FV fixture gate

The generator `tests/dpx/deep_fv_fixtures.py` produces two 16 KiB parent firmware volumes, each containing exactly one FFS. Both contain a child 4096-byte FV_IMAGE with two nested FFS entries.

- `NESTED.FV` embeds the FV image directly as a standard `EFI_SECTION_FIRMWARE_VOLUME_IMAGE` section.
- `UEFICOMP.FV` embeds the same FV_IMAGE inside `EFI_SECTION_COMPRESSION` with EFI-standard coding produced by the **real EDK2 TianoCompress utility in `--uefi` mode**, not a mocked compressor.
- The compiled DpX EFI program runs inside QEMU/OVMF Shell against both fixtures, verifies the printed `NestedFV FFSCount=2`, and for the second file asserts `Decompress=UEFI_STANDARD decodedBytes=4100`.
- Failure to decompress must never affect the correctly verified physical parent FV Used/Free. This test is not permission to assume all GUID-defined/Tiano/LZMA/Brotli codecs are supported.
