# DpX.efi Development Plan v1.3 — All-in-One UEFI Shell

Date: 2026-10-19. Status: **DOC UPDATE ONLY**. No DpX.efi implementation, build, or tests have been executed.

This supersedes [Development Plan v1.2](DpX_Development_Plan_v1.2.md) on architecture and delivery scope. Detailed FPDT, reliability, FV space-accounting, security and acceptance cases from [v1.2](DpX_Development_Plan_v1.2.md) and [FV Spec v1.1](DpX_FV_Analyzer_Spec_v1.1.md) remain in effect unless explicitly superseded here.

## 1. Mandatory product decision

**All requested core functions must execute inside ONE DpX.efi application in UEFI Shell.** No separate Windows/Linux Host parser is required to calculate FV capacity, parse a local ROM image, compare two local ROM files, collect multi-boot measurements, or inspect available raw SPI ROM. A Host GUI may be built separately later, but is optional.

Platform permissions still apply. The presence of an EFI command does not guarantee a particular computer permits reading every SPI Flash region. The application must explain unavailable sources rather than fabricate results or bypass protections.

## 2. All required command families

| Family | Proposed Shell CLI | Deliverables |
|---|---|---|
| Performance | DpX.efi -summary / -top / -tree / -gap-ms / -validate | FPDT/FBPT records, inclusive duration, phase, warnings, attribution |
| System | DpX.efi -sysinfo | UEFI, SMBIOS, ACPI, CPUID, PCI, TCG2, clock and provenance |
| Benchmark | DpX.efi -bench N / -bench-status / -bench-summary / -bench-abort | Persistent distinct-boot snapshots, median/mean/P95, reset guard |
| FV/FFS | DpX.efi -fv-list / -fv-files / -fv-sections / -fv-space | All visible FV, FFS and Section metadata and sizes |
| ROM status | DpX.efi -rom-info / -rom-regions | Source, controller, descriptor region, read rights, supported backends |
| ROM dump | DpX.efi -rom-dump bios/full -out fsX:\path.bin | Explicit read-only SPI/FVB dump, bounded reads and integrity |
| File import | DpX.efi -rom-analyze -input fsX:\bios.bin | Parse local FV/FD/ROM/BIN image inside EFI Shell |
| ROM compare | DpX.efi -rom-compare -base fsX:\a.bin -target fsX:\b.bin | File/region/FV/FFS/Section sizes, hashes and additions/removals |
| Correlation | DpX.efi -rom-fv-report -input fsX:\bios.bin -map fsX:\bios.map | FPDT module GUID, FV, FFS, PE/TE and best-effort RVA symbol mapping |
| Export | DpX.efi -snapshot / -json / -csv / -out | Binary raw FPDT/FBPT, JSON, CSV, TXT with explicit data source |

**All of the above are planned interfaces, not currently implemented commands.**

## 3. FV TOTAL / USED / FREE / USAGE %: mandatory

Every individual FV always has four output fields, even if a field is not available.

- TOTAL: verified FV header FvLength; reported-only size is labeled firmware-reported.
- USED: verified physically allocated or reserved FV bytes, including header, FFS headers and payloads, PAD FFS, alignment/reserved bytes and deleted-but-unreclaimed areas.
- FREE: verified unallocated erase-state bytes in original raw FV layout.
- USAGE: 100 x USED / TOTAL, if both are valid.
- Require USED + FREE = TOTAL, with exhaustive nonoverlapping byte ranges and no unexplained gaps. If full raw FV bytes are not available or validation fails, USED/FREE are N/A with reason. Never subtract logical FV2 file sizes to manufacture Free.
- For nested FVs, the child occupies bytes already accounted for in the parent FFS payload. Aggregate sums exclude duplicates/overlap.
- **DpX.efi must calculate sizes directly from an imported complete supported FV/ROM file on Shell filesystem.** Direct SPI reading is not a prerequisite for this file-based mode.

## 4. Direct ROM/SPI read policy

1. Detect FV2, FVB, PI SPI IO and applicable, documented vendor/Intel controller read backend. Probe permissions with conservative reads; unknown means unknown, not readable.
2. ROM dump is explicitly requested, never automatic. Only perform permitted read transactions. Do not write/erase flash, change descriptor/controller lock bits, bypass protections, change BootOrder or modify firmware.
3. BIOS Region dump only accesses validated allowable ranges. Full IFWI dump is tagged COMPLETE only when every requested byte is read and validated. A hole, denied region, truncation or disk-full is not a full dump.
4. Partial dump requires explicit permission and a manifest listing each missing span and its status. Never silently fill gaps with 0xFF and call them complete.
5. Read in bounded chunks; use checked 64-bit sizes and offsets, flushing/readback verification, checksum and cancellation. Avoid allocating an entire ROM image in memory.
6. Intel PCH backends must be gated by supported PCI/device/revision/controller model and applicable documentation. Unsupported platforms return UNSUPPORTED. No universal NVLS/NBLS SPI access assumption.
7. Proprietary, encrypted, CSME, ACM, FSP or third-party binary content may be represented as opaque areas with hashes/offsets only when recognizable; no false full decoding or automatic upload of firmware data.

## 5. Direct ROM file analysis and comparison inside EFI

- Accept local .fv, .fd, .rom and .bin. Detect FV candidates using validated PI header and bounds, not signature alone.
- Analyze FV header, FFS2/FFS3, FFS physical size, padding, deleted state, section types, compression/GUID-defined/nested FV, file hash and non-overlap.
- Compare two file paths with bounded chunked reads. Report global file hash/size, matched FV/FFS/Section changes, offsets, additions, removals, and unknown areas.
- Match FPDT module full GUID with FFS GUID and optional externally provided matching MAP/RVA information. Ambiguous keys must stay ambiguous. Do not claim compression caused performance latency solely from association.
- All analyses happen through the same DpX.efi; a later Host viewer is optional.

## 6. Multi-boot and measurement integrity

Persistent sessions capture one run per distinct boot, save validated FPDT plus selected system metadata, and calculate median/mean/min/max/StdDev/P90/P95. The storage location is a validated device path, not assumed from fs0 mapping across boots.

Auto reset is opt-in, bounded by N, depends on confirmed Shell reentry setup and only follows a successfully committed snapshot. Abort/failed write/read-only volume must not create an infinite boot loop. EfiResetCold is not a physically verified AC power cycle.

ROM Dump and deep FV scans are **disabled by default during timing benchmarks**, preserving boot duration semantics; their collection time is reported separately as overhead.

## 7. Internal module boundaries

- Cli: Shell argument dispatch and help.
- Capture and PerfAnalysis: FPDT/FBPT, trace, phase, top, gap and tree.
- Inventory: CPU, BIOS, memory, PCI, ACPI, TPM, security and provenance.
- FvAnalyzer: runtime FV2/FVB listing and raw PI FV/FFS/Section accounting.
- RomAccess: platform-aware read-only controller, region and permission backends.
- RomImageParser: streaming local firmware-image parsing.
- RomDiff: Shell A/B comparison and hashes.
- SymbolResolver: full FFS GUID and optional supplied MAP/RVA.
- Session, Storage and Export: crash-safe files, multi-boot state, raw/JSON/CSV/TXT.

Create independent package/application sources under CommonToolsPkg, without editing pinned edk2 or edk2-platforms submodules.

## 8. Version gates

| Gate | Mandatory outcome |
|---|---|
| M0 Docs | Unified all-in-one scope, CLI, safety, error codes, raw FV and ROM fixtures |
| M1 Shell Application | Build standalone X64 DpX.efi and command help |
| M2 Parsers | Safe FPDT/FV/ROM parsers and exact FV accounting on synthetic images |
| M3 Analysis | System inventory, performance profiling, FV2 enumeration and ROM file import in Shell |
| M4 ROM & sessions | SPI capability probe and allowed read-only dump, A/B ROM compare, multi-boot storage |
| M5 Controlled reset | 3+ boots, loop guard, recovery, valid environment comparisons |
| M6 Release | One DpX.efi with all listed command families, test evidence, SHA-256 and user guide |

Do not claim completion from compiling alone. Record Windows/Linux compile, OVMF Shell test, and actual-board firmware/SPI test as separate statuses. A board refusing SPI reads can still pass restricted-read error handling; full-dump success must be demonstrated on a platform with verified permission, not invented.

## 9. References and tracking

- [DpX requirements / Multi-Boot Issue #3](https://github.com/cdshopping001-coder/Edk2Test/issues/3)
- [FV Analysis Issue #5](https://github.com/cdshopping001-coder/Edk2Test/issues/5)
- [SPI/ROM All-in-One Issue #8](https://github.com/cdshopping001-coder/Edk2Test/issues/8)
- [Detailed original Development Plan v1.2](DpX_Development_Plan_v1.2.md)
- [Detailed FV Specification v1.1](DpX_FV_Analyzer_Spec_v1.1.md)
- [TianoCore DP in stable202608](https://github.com/tianocore/edk2/tree/edk2-stable202608/ShellPkg/DynamicCommand/DpDynamicCommand)
- [PI SPI IO protocol definition](https://github.com/tianocore/edk2/blob/edk2-stable202608/MdePkg/Include/Protocol/SpiIo.h)

**Document only. No source/build/workflow files are changed by this revision.**
