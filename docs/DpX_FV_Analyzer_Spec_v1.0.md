# DpX Firmware Volume (FV/FFS/Section) Analyzer — Detailed Feature Specification v1.0

> Date 2026-10-19 | Parent: [DpX Development & Delivery Plan v1.1](DpX_Development_Plan_v1.1.md) | Tracking: GitHub issue #3 and FV subtask
> Scope: **read-only EDK2 runtime FV inspection** + **optional offline ROM parsing in a separate host process**. This document is a design specification; no FV parser, application or ROM analyzer is yet implemented.

## 1. Objective and supported data sources

Answer: which Firmware Volumes and FFS files are visible; how large they are; which sections exist; where performance-significant PEIM/DXE modules reside; whether section compression may contribute to measured time; and whether files/FVs changed between BIOS versions.

| Tier | Execution | Input | Can substantiate | Cannot guarantee |
|---|---|---|---|---|
| FV-0 | DpX.efi in Shell | `EFI_FIRMWARE_VOLUME2_PROTOCOL` | Visible FV handles, file GUID/type/attributes/size; selectively readable sections; source protocol status | Physical FV header/offset, erased space, entire SPI/IFWI |
| FV-1 | DpX.efi in Shell, opt-in | `EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL`, memory mapped address when valid, bounded read | Block map / FV raw header, file layout and padding/free ranges only when raw bytes safely readable and validated | SPI address = physical memory address; all platform flash regions |
| FV-H | Windows/Linux Host, separate | Provided *.fd/*.fv/*.rom/*.bin IFWI | Candidate FV headers/FFS/sections, offsets, nested FV, raw occupied/free mapping and hashes | Opaque/proprietary encapsulation; encrypted/signed silicon blobs; full meaning of Intel descriptor/CSME |
| FV-C | Offline comparison | Two validated FV snapshots or ROM images | Added/removed/moved/resized FVs/FFS, compression/section differences, module association | Equal GUID always implies same bytes/function or execution |

**Key reliability rule:** FV2 `GetNextFile` provides a logical file view. Its returned `Size` and `ReadFile` data are **not** a sufficient accounting of byte-accurate on-flash FFS header/alignment/padding/erased space. Report `capacity/physical_used/free` as `N/A` unless derived from validated raw FV bytes. Treat FV size obtained from FV2's optional `GetInfo` or other reported metadata as firmware-reported, not a raw-media measurement.

## 2. Dependencies and authoritative EDK2 baseline

- [FirmwareVolume2.h](https://github.com/tianocore/edk2/blob/edk2-stable202608/MdePkg/Include/Protocol/FirmwareVolume2.h): `GetVolumeAttributes`, `GetNextFile`, `ReadFile`, `ReadSection`, `GetInfo`.
- [FirmwareVolumeBlock.h](https://github.com/tianocore/edk2/blob/edk2-stable202608/MdePkg/Include/Protocol/FirmwareVolumeBlock.h): `GetPhysicalAddress`, `GetBlockSize`, `Read`; do **not** call `Write` / `EraseBlocks`.
- [PiFirmwareVolume.h](https://github.com/tianocore/edk2/blob/edk2-stable202608/MdePkg/Include/Pi/PiFirmwareVolume.h): `EFI_FIRMWARE_VOLUME_HEADER`, `FvLength`, `HeaderLength`, `ExtHeaderOffset`, `FileSystemGuid`, block map, checksum, erase polarity.
- [PiFirmwareFile.h](https://github.com/tianocore/edk2/blob/edk2-stable202608/MdePkg/Include/Pi/PiFirmwareFile.h): `EFI_FFS_FILE_HEADER` / `EFI_FFS_FILE_HEADER2`, `FFS_ATTRIB_LARGE_FILE`, sizes, state, compression/guided/PE32/TE/depex/UI/nested-FV sections.
- [EDK2 Fv.py](https://github.com/tianocore/edk2/blob/edk2-stable202608/BaseTools/Source/Python/GenFds/Fv.py): reference generator behavior, not a substitute for independent parser validation.

## 3. Runtime FV enumeration: FVR-* requirements

- **FVR-01:** enumerate handles with `EFI_FIRMWARE_VOLUME2_PROTOCOL` and optionally `EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL` through `LocateHandleBuffer(ByProtocol)`. Never assume protocols refer to an identical volume without matching device path/evidence; de-duplicate only with a reliable identity key.
- **FVR-02:** per visible logical FV: capture FV identity (handle-local ID plus optional FV Name GUID/device path), firmware-reported attributes, read capability/status, file count, enumeration status, collection timestamp, and source/confidence. Handle value is diagnostic and **not stable across reboots**.
- **FVR-03:** enumerate FFS with `GetNextFile` and report full File GUID, FFS type, FV file attributes, API-returned `Size`, enumeration ordinal. Return status for each operation. No speculative physical offset from logical FV2 enumeration.
- **FVR-04:** `ReadFile` and `ReadSection` only when opt-in `-fv-sections` is enabled, with bounded buffers and explicit size/recursion limits. Some sections may already be transparent/decompressed by FV services, so FV2 output **cannot prove original compression algorithm or compressed size**. Such fields must stay `unknown` until raw section bytes are available.
- **FVR-05:** Do not invoke FV2 setters/writers, FV block writers/erasers, variable setters, protocol installers, dispatchers or decompress/untrusted EFI executables. Sampling should not alter firmware content.
- **FVR-06:** expose `available/unsupported/access_denied/partial/invalid` distinctly. Missing protocol, `EFI_PROTOCOL_ERROR` (missing guided extraction), and read denied are **not** fatal to FPDT capture; attach warning and continue normal profiling.
- **FVR-07:** when `-fv-raw` selected, require FVB read capability and verified memory map/block bounds. Use `Read` per-block with bounded transfer; `GetPhysicalAddress` is the address of **memory-mapped FV**, not necessarily original SPI offset. If unsupported, report `RAW_NOT_AVAILABLE`, do not dereference guessed flash addresses.
- **FVR-08:** compute exact raw layout metrics *only after* validating signature `_FVH`, header length/checksum, volume size, file-system GUID, block map totals, ext header, erase polarity, FFS2/FFS3 structure and aligned boundaries. Limit total bytes/depth/file/section count; abort parsing on any integer overflow or truncated data.
- **FVR-09:** raw layout must distinguish `header_bytes`, `live_file_bytes`, `file_header_bytes`, `alignment_padding_bytes`, `pad_file_bytes`, `deleted_or_invalid_bytes`, `erasable_free_bytes`, `unknown_bytes`. Avoid `free = fvlength - sum(GetNextFile Size)`. Physical occupied/free sums must reconcile to FV length; if they do not, mark accounting invalid.
- **FVR-10:** section parser supports `RAW`, `UI`, `VERSION`, `PE32`, `TE`, `PEI/DXE/MM DEPEX`, `COMPRESSION`, `GUID_DEFINED`, `FIRMWARE_VOLUME_IMAGE` and unknown. Preserve raw section bytes and header metadata; optional decompression only through supported, explicit, trusted methods. For unknown GUID-defined compression, preserve raw and mark `not_decoded`.
- **FVR-11:** nested FV parsing must cap maximum nesting depth, per section allocation, cumulative decompressed bytes, and total traversal time/records; prevent crafted malformed FV inputs from exhausting pre-boot resources.
- **FVR-12:** do not automatically export firmware binary/embedded code on a production system. Default read-only listings/metadata; raw firmware export is explicit opt-in, stored locally, never pushed to GitHub.

## 4. Performance correlation: FVP-* requirements

- **FVP-01:** join FPDT record full Module GUID / FILE_GUID to the FV/FFS inventory where a full, unambiguous match exists. Distinguish `exact_guid_match`, `candidate`, `ambiguous`, `not_found`. Data1 alone is not unique, and FPDT `Handle` is not stable.
- **FVP-02:** link module `Token`, `Phase`, inclusive measured duration, and FFS attributes only as **association**; do not claim that compressed FFS caused the duration without decompression-specific instrumentation.
- **FVP-03:** repeatable module file GUID across multiple FVs or revisions remains ambiguous. Keep full FV identity and provenance; don't arbitrarily select a FV.
- **FVP-04:** optionally show original/raw compressed vs decompressed sizes and codec **only when the original section encoding can be inspected**. FV2 extracted `ReadSection` is not enough. `compressed_ratio` otherwise N/A.
- **FVP-05:** allow filtering: phase, FV identifier, FFS GUID/type, compression status, top N duration, file size, and missing/ambiguous matches.
- **FVP-06:** do not include FV scanning time in firmware boot duration. For benchmarks choose `-fv-check off|once|each`: `off` default for quick runs; `once` baseline after initial FPDT capture; `each` captures each boot for changes. Report collection overhead and note that `once` does not prove FV stayed unchanged after the first boot.

Example report (illustrative only; not real measurements):

```text
FVs visible: 4 (via FV2, logical); raw layout validated: 2/4
FV: 8DA1...  source=FV2  files=78  capacity=N/A  free=N/A
  FFS: A0C98B77-....  DXE_DRIVER  logicalSize=84 KiB  physicalOffset=N/A
  FPDT: token=P:A0C98B77.000039C0  duration=266ms  match=CANDIDATE
  section: PE32 available=yes  compression=UNKNOWN (raw bytes unavailable)
Warnings: 2 FVs lack FVB raw access; no physical flash-offset claim.
```

## 5. Offline Host ROM: FVH-* requirements (independently delivered)

- **FVH-01:** accept user-supplied `.fv`, `.fd`, `.rom` or `.bin`; recognize FV candidates by signature and structural checks, *not* by signature alone.
- **FVH-02:** recursively inspect FV/FFS/section structures where supported; recover exact byte ranges, checksums, alignment/padding, deleted FFS state, free/erased spans, nested FV, section compression/encryption status and per-section hashes.
- **FVH-03:** report container offsets relative to the **input image**. Do not interpret input file offset as physical flash offset unless descriptor/base/address mapping is explicitly validated.
- **FVH-04:** support differential inventories, changed FFS and compression/size changes; detect likely duplicate FV images/hashes.
- **FVH-05:** proprietary IFWI regions such as Intel CSME, embedded FSP, ACM, microcode and OEM-specific binaries may be indexed as opaque regions when identifiable; do not imply complete decoding or redistribution rights.
- **FVH-06:** preserve all unrecognized bytes and emit warnings; robust malformed file handling, recursion/decompression limits and fuzz tests.
- **FVH-07:** output mapping file suitable for symbol resolver with FV/FFS identity and file offsets, only if sufficient matching build input exists.

## 6. CLI (proposal, not yet implemented)

```text
DpX.efi -fv-list
DpX.efi -fv-files -fv 0
DpX.efi -fv-files -fv 0 -type DXE_DRIVER
DpX.efi -fv-sections -guid <FULL-FFS-GUID> -fv 0
DpX.efi -fv-summary -json fs0:\DpX\fv-summary.json
DpX.efi -fv-raw -fv 0 -out fs0:\DpX\fv-0000.raw
DpX.efi -top 20 -min-ms 50 -fv-correlate
DpX.efi -bench 20 -session NVLS_DPF_RC2 -fv-check once -out fs0:\DpX

# Host: future independent utility
dpxhost fv parse BiosImage.bin --json fv.json --map fv-map.json
dpxhost fv diff biosA.bin biosB.bin --report fv-diff.html
```

`-fv 0` is the enumeration index for the **current invocation**, not a cross-boot stable identifier. `-fv-raw` must be a separate explicit action. All FV reads are read-only.

## 7. Data contract & security

Per FV: `schema_version`, `source`, `status`, `confidence`, `device_path_hash` where available, `fv_name_guid`, `fv2_attributes`, `fvb_status`, `raw_access`, `layout_validated`, `capacity_bytes` nullable, `file_count`, `volume_hash` if raw bytes available, `warnings`.

Per FFS: `file_guid`, `type`, `attributes`, `logical_file_size`, `physical_file_size` nullable, `physical_offset` nullable, `sections`, `validation_status`. Per section: `type`, `raw_size`, `decoded_size` nullable, `compression_type` nullable, `guid_defined_processing`, `checksum/sha256`, `origin`. Per correlation: `record_id`, `file_guid`, `fv_identity`, `match_confidence`, `reason`.

Use decimal strings for uint64 values in JSON where JavaScript precision may be lost. Hashes are for identification/integrity, not proof of firmware authenticity. Raw firmware blobs may contain secrets or licensed third-party code; keep them out of public CI and artifacts by default.

## 8. Milestones / test gates

| Existing DpX stage | FV deliverable | Gate |
|---|---|---|
| M0 specification | This document, FV schemas and malformed/synthetic FV fixtures | Reviewed capability boundaries |
| M1 package build | FV parser API stubs and dedicated test harness | VS2022/GCC compile, existing DpX remains functional |
| M2 parser safety | Host-testable FV header/FFS/section reader, bounds validation | Fuzz and malformed/large file tests |
| M3 single-boot | `-fv-list`, `-fv-files`, `-fv-summary` via FV2 | OVMF Shell lists visible FVs, absent FV2 safe |
| M4 benchmark integration | Optional FV fingerprint + FPDT Module GUID correlation | No boot-duration pollution; ambiguous match shown |
| M5 auto-reset | Honor `-fv-check off|once|each`, no unsafe reads during reset flow | Fail-safe session state and no double count |
| M6 release | Documented FV raw mode if tested; final output and compatibility matrix | OVMF + real hardware read-only checks |
| P1 Host | Full offline ROM/IFWI inspection + FV comparison + RVA/MAP resolver | Separate Host tests, not a false v1.0 EFI claim |

**Must-test:** FV2 absent, FV2 access denied, logical GetNextFile duplicates, malformed header/header checksum, FFS extended size, alignment/padding, erase-polarity, deleted-state FFS, GUID-defined unavailable decoder, nested-FV decompression bomb, invalid raw physical pointer, FVB partial reads, >4GiB sizes, GUID duplication across FVs, Section payload compressed vs decoded confusion, lack of free-space access, runtime collection overhead, missing raw dump permissions, user abort.

## 9. Acceptance checklist

- [ ] FV2 enumeration reports reliable source/status; no guessed SPI region offsets or capacity.
- [ ] Correct raw FV size/used/free **only** when entire bounded raw layout passes validation, otherwise N/A with diagnostics.
- [ ] Section types identified; original compression/codec reported only with raw encoding evidence.
- [ ] Performance association explains GUID ambiguity; no inferred causal relationship between file size/compression and elapsed time.
- [ ] Multi-Boot FV collection optional and does not modify boot interval metrics or force reboots.
- [ ] Read-only behavior verified: no FV2/FVB writes, flash writes, variable writes, setup changes or EFI image dispatch.
- [ ] Unit, integration, negative and regression tests pass; Windows/Linux/OVMF/real-hardware results documented separately.
- [ ] Raw firmware exports never automatically uploaded to public repository or CI.

> Implementation tradeoff: **FV2 logical inventory in DpX.efi first, raw FV layout cautiously as optional, full firmware image structure in DpXHost**. This provides useful performance correlation without making a generic Shell tool a vendor-specific flash dumper.
