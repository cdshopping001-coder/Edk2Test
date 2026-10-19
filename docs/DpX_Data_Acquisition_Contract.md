# DpX acquisition-first development gate

## User decision — 2026-10-19

**Complete DUT data acquisition and integrity/coverage verification before selecting report verbosity, tree depth, Top-N rules or visual layout.** Treat all existing Shell tree text as a **provisional diagnostic preview**, not the final report contract.

The only mandatory user action is still `Shell> DpX.efi` on the actual test device. No ROM, FBPT, MAP or FPDT input is required. All access is read-only; external file parsers are optional testing aids.

## Collection-first architecture

1. **DP raw collection:** obtain FBPT from firmware ACPI FPDT or upstream EDKII extended FPDT Configuration Table. Copy the complete validated record buffer (not only first N records); retain record Type, Length, Progress ID, APIC, time, Module GUID, Trigger GUID and tokens without losing unknown types. Record the number of bytes and whether the source was truncated, missing or inaccessible. Never infer Windows loader timestamps while still in Shell.
2. **FV / FFS acquisition:** enumerate every published FV2 and readable FVB handle; capture FFS GUID, file type, FV index, sizes, state and, when known, physical FVB-relative offset. Read all safely accessible sections, decompress supported codecs and inspect nested FV. Preserve parent/child provenance, physical-versus-decompressed byte counts and explicit per-item partial/error status. Never claim access to hidden or protected Intel SPI regions.
3. **Module identity catalog:** store **each** FFS occurrence, including duplicate GUIDs, and any UI name extracted from supported sections. Maintain a separate exact GUID→unique/ambiguous resolver, keeping Trigger GUID distinct from Module GUID. Existing bounded implementation stores up to 4096 on-board FFS metadata rows; reaching that limit is a PARTIAL capture, not a complete dataset.
4. **Time correlation:** only once raw event and FFS data have been acquired, compute valid Start/End pairs, any timestamp containment and matched module labels. Keep unmatched, missing, malformed, cross-CPU and unknown records explicitly counted. No asserted function symbol without matching symbol source.
5. **FV capacity:** from trustworthy raw FVB layout account for Total/Used/Free, PAD, deleted/unreclaimed and nested child logical occupancy without double counting; record source and reason for N/A.

## Reporting gate — deliberately deferred

**Do not commit to a final Shell text hierarchy, default tree depth, verbosity, Top-10/Top-N cap, CSV/JSON schema or graphical layout yet.** First demonstrate that acquisition is comprehensive within the permissions of the DUT and that a separate analysis layer can consume the captured dataset. Output format is a later user decision.

## Current verified versus remaining collection work

- Implemented: bounded live FBPT snapshot, FV2 FFS GUID/UI cache and per-record metadata capture, FVB read-only physical inspection, standard PI compression and nested FV parser, and exact GUID correlation where visible.
- Pending: persist a full section/nested-FV topology catalog instead of printing and discarding it; capture all valid physical FFS coordinates in a unified dataset; provenance and completeness metadata on every result; memory/size pressure handling across large production ROMs; real NVLS/NBLS Citadel/RVP validation.
- Not provided by a GUID alone: function address/RVA or matching PDB/MAP. Mark Function=N/A until supported evidence exists.

## Acceptance before report tuning

- Actual `DpX.efi` executes without input files.
- Every collection source reports records found, skipped, decode failures, truncation limits, and inaccessible regions.
- Tests include duplicate GUIDs, malformed/unsupported compressed sections, hidden/unsupported FV ranges, and invalid/unpaired FPDT records.
- Both GCC and VS2022 builds pass, and OVMF Shell confirms the collector code path. **OVMF does not prove access to protected Citadel SPI or platform-specific performance records.**
