# DpX v0.4 — Raw FBPT Basic Boot Parser

The UEFI Shell application implements `DpX.efi -perf-parse -input fs0:\\BOOT.FBPT` for an **already acquired, complete FBPT byte stream**. This is the first slice of FPDT implementation, and does **not** yet fetch live ACPI FPDT pointers or recreate TianoCore DP's Start/End matching algorithm.

## Parsed fields

Per ACPI 5.0 `EFI_ACPI_5_0_FPDT_FIRMWARE_BASIC_BOOT_RECORD`: `ResetEnd`, `OsLoaderLoadImageStart`, `OsLoaderStartImageStart`, `ExitBootServicesEntry`, `ExitBootServicesExit`. It reports raw values (without misleading Shell-to-OS boot-time conversions). If loader values are absent in a Shell boot, `WindowsBootMetrics=N/A` is expected. For a valid monotonic sample the parser reports loader Load→Start and ExitBootServices Entry→Exit deltas.

The FBPT Header signature and length are validated; each performance record has an enforced 4-byte minimum and strict bounds, unsupported Basic Boot revision, duplicate and truncated Basic Boot are rejected, unknown record types counted and skipped safely. File access uses fixed-size reads with a 64 MiB maximum, never loads full performance table into preboot memory.

## CI verification

`tests/dpx/test_fbpt_fixtures.py` produces synthetic 64-byte FBPTs with one Basic Boot (48 bytes) and one unknown performance record (8 bytes). Variants include a zero record length, bad signature and missing OS timestamps. The **actual compiled DpX.efi** must execute within QEMU/OVMF Shell and print exact values. Python fixture tests alone do not count as runtime validation.

Not yet implemented: live ACPI FPDT lookup, extended module records decode, phase timing, Start/End pairing, event attribution or multi-boot capture. This milestone keeps existing FV and ROM commands intact.
