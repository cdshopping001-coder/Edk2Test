# DpX 0.14 — Five-Stage Raw DP → FFS → GUID → Tree → FV Summary

**Authoritative default:** `Shell> DpX.efi`. This runs solely on the DUT; no external ROM or FPDT file is required.

1. **Collect DP raw records:** locate ACPI FPDT and safely read FBPT after Memory Map validation. If ACPI FPDT is absent, use the same `gEdkiiFpdtExtendedFirmwarePerformanceGuid` configuration table fallback used by the upstream EDK2 Shell `Dp.efi`, with the same guarded FBPT reader. Capture a bounded copy of the validated live FBPT rather than analyzing it before mapping.
2. **Decode firmware-visible FFS:** enumerate published FV2 volumes and FFS; walk Section/Compression/Nested FV where available and bounded. Physical FVB regions not published through FV2, privileged/hidden/PEI/SMM volumes and unsupported private compression must not be represented as fully decoded.
3. **Map module GUIDs:** full 128-bit FFS GUID and UI Section give the module label. `Guid1` is the module identity on dual-GUID records, `Guid2` is Event/PPI/Protocol; no function name is asserted without trustworthy token/RVA and build symbols.
4. **Performance records tree:** correlate exact Start/End pairs and order by APIC, start timestamp and end timestamp. A child is shown only for full **timestamp containment**, never claimed as proven call hierarchy. All matched pairs are retained up to a 4096-interval cap, 2048 are printed by default; the report must identify partial coverage at either limit. Unpaired, malformed or overlapping intervals are not subtracted as exclusive time. Top 10 remains available as secondary guidance.
5. **FV summary:** report Total/Used/Free only for real FVB-readable physically validated FVs. PAD, alignment and deleted-but-not-erased FFS bytes are included in Used, not Free; decompressed/nested FV logical bytes are never added to parent physical size.

Unmatched/unsupported data must print explicit N/A. Shell screenshots and a fixture-based OVMF/QEMU guest test are not a substitute for actual Intel NVLS/NBLS Citadel or RVP hardware validation.

## Test gates

- GCC and VS2022 standalone EFI binaries build.
- Actual OVMF Shell invokes `DpX.efi` without arguments and validates five stages.
- Separate synthetic FBPT with real PciBusDxe module GUID, callback Start/End and trigger GUID validates 120 ms and tree output.
- Corrupt FBPT/FV header and oversized/missing source cases remain bounded and truthfully marked partial.

Original raw record bytes are captured from firmware and used for the tree; this does NOT imply the user must provide exported `dp` text.
