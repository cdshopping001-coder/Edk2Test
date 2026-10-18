# DpX.efi DUT-first operation (authoritative design correction)

Date: 2026-10-19. This requirement supersedes any previous implication that users must export ROM or FPDT data before analysis.

## Primary path — run locally on the test machine

Copy **one `DpX.efi`** to the machine's UEFI Shell and run:

```text
Shell> DpX.efi
Shell> DpX.efi -summary
Shell> DpX.efi -perf-live
Shell> DpX.efi -fv-space
```

**No input .rom, .fv, .fpdt, .fbpt, MAP or Host program is required** for the default local inspection. All results describe the *current* machine. The binary must be read-only unless the user explicitly requests an export or a bounded reboot experiment. No automatic reset, flash write, descriptor unlocking or NVRAM changes.

Default (`DpX.efi` with zero args, or `-summary`) runs these live source queries in order: (1) UEFI/CPUID/variable inventory; (2) firmware ACPI FPDT and (only after UEFI Memory Map verification) FBPT Basic Boot; (3) FV2 live volume info and honest physical capacity availability; (4) SPI/FVB protocol presence / access availability. Each category is independently best-effort; missing FPDT or restricted FV/SPI must remain N/A with explicit reason rather than abort the whole report.

`-perf-live` defaults to requesting the on-board FBPT after validating the ACPI FPDT and its UEFI Memory Map range. `-perf-live -pointers` only inspects FPDT pointer metadata; legacy `-boot` remains accepted.

## Important limitations, not hidden behind the CLI

- Implemented: runtime ACPI FPDT discovery, validated FBPT Memory Map reader, UEFI system/CPU info, FV2 logical enumeration and SPI/FVB protocol discovery.
- Not yet implemented: raw FV physical USED/FREE from DUT when no raw access backend exists; complete FPDT event Start/End profiler and gap/top/tree; vendor-specific Intel PCH SPI reader/full IFWI; durable multi-boot sessions. These must have explicit N/A / unsupported output and remain release gates.
- **Offline file commands are optional**: `-rom-analyze -input`, `-rom-scan -input`, `-perf-fpdt -input`, `-perf-parse -input`. Keep them for regressions, image comparisons and fixture tests, never as required steps for the on-board workflow.
- The UEFI Shell's current FPDT Boot data does not necessarily contain OS-loader and ExitBootServices completion timestamps. Never claim Windows Last BIOS Time/POST from a Shell-only snapshot.
- FV `TOTAL/USED/FREE` should be recovered from available read-only live FVB or supported PCH SPI sources if possible. If not, show N/A and the exact restriction rather than using FV2 logical file sizes.

## Acceptance

CI must boot QEMU/OVMF, execute the actual `DpX.efi` **with no parameters** plus `-summary`, verify all local sections are printed without supplying external input, and preserve existing fixture-based regression tests. Follow with real Citadel/NVLS/NBLS tests for FPDT and raw FV availability.

**The user-facing success criterion is a useful read-only DUT report immediately after typing `DpX.efi`, not the ability to parse an exported ROM file.**
