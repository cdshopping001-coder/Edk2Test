# DpX 0.6.0 — Live ACPI FPDT Discovery

## CLI

`DpX.efi -perf-live`

Reads firmware-published ACPI FPDT via TianoCore EfiLocateFirstAcpiTable, as used by upstream Dp.efi. It validates table signature, bounded size (<= 1 MiB), complete checksum, record boundaries, pointer record sizes and revisions, and duplicate pointer records. Basic Boot and S3 pointer addresses are printed for diagnosis but NEVER dereferenced.

The existing file-backed -perf-fpdt and -perf-parse options remain available. No SPI writes, arbitrary physical memory read, or reset state changes are performed.

## Missing FPDT is not a failure of the probe

If OVMF provides no ACPI FPDT, -perf-live prints `Live FPDT status=unavailable reason=NOT_PRESENT_IN_ACPI`; it does not invent timestamps. QEMU/OVMF Shell CI executes the actual EFI binary and checks that this command provides a trustworthy result.

## Still not implemented

Live FBPT memory capture, validated physical allocation lookup, Phase/Module Start/End pairing, FPDT-to-Windows time comparison, and Citadel/RVP hardware testing. See Development Plan v1.3.
