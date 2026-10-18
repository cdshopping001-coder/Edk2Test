# DpX v0.10 — Resolve FPDT GUID to FFS/Module on DUT

`DpX.efi -guid-map` builds a **local**, bounded FFS GUID listing from each UEFI FV2 Protocol using GetNextFile. For each FFS it asks firmware's `ReadSection(EFI_SECTION_USER_INTERFACE)` for a readable module name. This call can traverse nested compression/GUID-defined encapsulation **when the platform has installed the corresponding extraction support**. The caller supplies a fixed 254-byte UI data buffer; unsupported, oversized or malformed UI data is reported N/A. It does not execute discovered PE/TE images.

`DpX.efi -guid-resolve xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` finds exact 128-bit FFS GUID matches and displays logical FV index, File Type, logical file size and optional decoded UI module name, with confidence/ambiguity. No substring/Data1-only matching.

**Important FPDT semantic distinction:** Extended FPDT GUID Event Record's first GUID may mean the *referenced module* for small ProgressID values or the *logging module* otherwise. For Dual GUID Callback/Event records **Guid1 identifies the module logging**, while **Guid2 identifies Event/Protocol/PPI**. Guid2 must **not** be resolved as if it were always an FFS module GUID.

**Function mapping is conditional:**
- GUID alone -> potential module (possibly anonymous). It does **not** identify a callback function.
- Record carries debug token -> may provide a label; do not call an ambiguous token a symbol or source function.
- Image address/RVA + matching PE/TE loaded-image address and original build MAP/PDB/ELF -> resolve an actual function and `symbol+offset` with build identity verification. On-device images often have stripped debug data. Merely decompressing the image does **not** reconstruct removed function symbols or call stacks.
- To map `CallbackFn` reliably in a performance trace, optionally instrument the registration site with Callback code address / RVA, or correlate known registration maps from the exact BIOS build.

**Limits:** FV2 only includes firmware-published volumes, not necessarily every ROM FV / PEI/SMM / protected SPI region. `ReadSection` may return decoded content without exposing the **original compression algorithm or physical section offset**. Raw FV/FVB accounting remains a separate physical view. Duplicate GUID matches produce AMBIGUOUS; no matches produce NO_VISIBLE_FFS_MATCH, not an invented name.

Live default `DpX.efi` stays DUT-first and does not scan/decompress all FFS at every invocation; use explicit `-guid-map` or `-guid-resolve` to control overhead. End-to-end automatic annotation of DP event lines and PDB/MAP symbolization is a separate integration gate, not falsely claimed complete here.
