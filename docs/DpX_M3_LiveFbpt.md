# DpX 0.7 — Safe Live FBPT Read (opt-in)

`DpX.efi -perf-live` validates ACPI FPDT and prints pointer diagnostics without touching the FBPT physical memory address.

`DpX.efi -perf-live -boot` additionally attempts to read the firmware-published FBPT, but only after obtaining the real UEFI Memory Map and confirming that both the fixed FBPT header and the **complete reported table length** lie within ONE allowed memory descriptor. Allowed types: EfiReservedMemoryType (used by upstream FirmwarePerformanceDxe), EfiACPIReclaimMemory, EfiACPIMemoryNVS, EfiBootServicesData. Zero, overflow, unmapped, unsupported, or partial ranges return N/A / ACCESS_DENIED and must not be dereferenced.

Once the complete memory range is validated, DpX walks the bounded FBPT record headers, verifies Basic Boot length/revision/uniqueness and prints ResetEnd and OS Loader / ExitBootServices raw timestamp fields. A Shell boot typically has missing OS-loader timestamps, reported as N/A. It does not equate Shell timing to Windows firmware POST.

No flash writes, variable writes, controller lock modification, or arbitrary address input. The Boot pointer originates only from the checksummed live ACPI FPDT. The OVMF test verifies both CLI paths execute, but OVMF without FPDT **cannot validate successful live FBPT reading**; this remains a separate platform acceptance gate for Citadel / Intel RVP.

Still not implemented: full callback start/end pairing, FPDT extended debug fields, multi-boot sampling, and real-board validation.