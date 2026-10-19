# DpX 0.13.0 — Nested Tree View for UEFI Shell

## Primary UX

Run `Shell> DpX.efi` without any input file. The six report sections now use visible branches and the FV/FFS/Section path is rendered as a **hierarchical tree**, not as a flat inventory. `DpX.efi -fv-detail` is the focused firmware-tree command.

UEFI consoles vary in their Unicode glyph sets, so the device-visible format intentionally uses safe 7-bit ASCII: `|--`, \`--, and vertical `|   ` indentation. This is equivalent to ├──, └──, │. Tree depth is bounded and follows actual parsing recursion, rather than inferred output indentation.

## Firmware-volume tree

```text
|-- FV2[0] attributes=... (logical; byte offsets may be unavailable)
|   |-- FFS[0] GUID=... Type=... LogicalSize=...
|   |   |-- Section[PE32] depth=0 RawBytes=...
|   |-- FFS[1] GUID=... Type=... LogicalSize=...
|   |   |-- Section[COMPRESSION] depth=0 Stored=...
|   |   |   |-- Decompress=UEFI_STANDARD decodedBytes=... parent physical usage UNCHANGED
|   |   |   |   |-- Section[FV_IMAGE] depth=1 ...
|   |   |   |   |   |-- NestedFV depth=2 ... inside parent payload
|   |   |   |   |   |   |-- NestedFFS[0] GUID=... Type=...
|   |   |   |   |   |   `-- Accounting=CHILD_ONLY_DO_NOT_ADD_TO_PARENT
`-- LiveFVB[0] Total=... source=verified FVB raw layout
    |-- FFS[0] ... physical offset ... State=VALID
    |-- FFS[1] ... State=DELETED_UNRECLAIMED (used, not free)
    `-- FV Space Used=... Free=... Usage=...
```

The physical and logical trees are intentionally **distinct roots**: FV2 enumeration does not prove a unique physical SPI mapping, and FVB byte accounting must not be double counted with decoded/nested FV size. Unsupported private compression and inaccessible ROM stay N/A.

## Boot-event tree

```text
|-- DPX Extended FPDT: Records=... Events=... Pairs=...
|   |-- Top[1] DurationNs=120000000 Progress=0x0020 ModuleGuid=... Module=PciBusDxe
|   |   |-- TriggerGuid=... (Event/PPI/Protocol, not module)
|   |   `-- Token=PciEnumerator FunctionSymbol=UNVERIFIED_TOKEN
```

This is a **tree of verified *record relationships***, not a call stack. FPDT paired intervals and Module/Token are children of their event. There is deliberately no SEC→PEI→DXE→Callback causal nesting unless independent source evidence or instrumentation establishes those parent-child edges. Time overlap by itself does not demonstrate caller/callee relationships.

## Test gates

OVMF must execute the actual DpX.efi with zero args, `-fv-detail`, synthetic nested/compressed FV fixtures and Callback Start/End FBPT fixture. Assertions require tree branches around FFS, Section, NestedFV and Extended FPDT Top[1] (120,000,000ns). Existing size, decode and event-semantic tests remain unchanged.

FFS UI sections are now shown *inside* their owning FFS subtrees as a bounded decoded `UI Name=... source=FFS_UI_SECTION` node. A UI name is module metadata, not a function symbol. Unknown private/GUID decompression does not invent nested nodes.
