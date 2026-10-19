# DpX 0.15 — Five stages, coherent FV + temporal tree

Default on-DUT command: `DpX.efi` (no files).

1. **Collect raw DP records**: firmware ACPI FPDT/FBPT or supported EDK2 fallback, snapshot in RAM with bounds and explicit N/A.
2. **Enumerate/decompress visible FFS**: FV2 logical FFS, FVB physical FFS, PI sections, UEFI-standard compression and nested FV. Not every protected FV or private codec can be decoded.
3. **GUID to module**: full 128-bit FFS GUID and decoded UI, cached for event annotation. Protocol/PPI Trigger GUID is never reclassified as the Module GUID.
4. **Performance tree**: only paired Extended FPDT Start/End, on-APIC timestamp containment. ASCII `|--` tree, no fabricated caller/callee or unknown function names; Top 10 retained.
5. **FV summary**: use cached verified physical layout Total/Used/Free/PAD/deleted FFS from stage 2 — no second full-FV scan, no counting nested logical bytes in physical subtotal.

QEMU/OVMF Shell fixture additionally includes a nested 120 ms parent / 40 ms child event, validates both time durations and source mappings. Current platform ACPI may not publish FPDT, so N/A is legitimate. No flash writes, changes to NVRAM or automatic reboot. Intel Citadel/RVP field acceptance remains outstanding.
