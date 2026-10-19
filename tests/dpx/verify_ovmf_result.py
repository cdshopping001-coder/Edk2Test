#!/usr/bin/env python3
"""Check actual redirected UEFI Shell output, not a host reimplementation."""
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
output = data.decode("utf-16le", errors="replace") if b"\0" in data else data.decode("utf-8", errors="replace")
required = (
    "DPX_SMOKE_BEGIN", "DPX_SMOKE_DONE", "DpX 0.14.0",
    "Total=4096", "Used=136", "Free=3960",
    "status=verified_contiguous_layout",
    "reason=INVALID_FV_HEADER_OR_BLOCKMAP", "Used=N/A",
    "FV[0] ROM offset=0x1000", "FV[1] ROM offset=0x3000",
    "FV candidates=2 complete layouts=2",
    "FBPT signature=FBPT", "BasicBoot ResetEnd=500000000",
    "BasicBoot OsLoaderLoadImageStart=800000000",
    "ExitBootServicesDuration=50000000",
    "FBPT status=verified Records=2 BasicBootRecords=1 OtherRecords=1",
    "FBPT status=invalid reason=RECORD_LENGTH_INVALID",
    "WindowsBootMetrics=N/A reason=OS_LOADER_TIMESTAMPS_INCOMPLETE",
    "FPDT signature=FPDT",
    "BootPerformanceTablePointer=0x12345000",
    "S3PerformanceTablePointer=0x0",
    "FPDT status=verified Records=3 BootPointers=1 S3Pointers=1 OtherRecords=1",
    "FPDT status=invalid reason=ACPI_CHECKSUM_INVALID",
    "Live FPDT discovery source=firmware-published ACPI XSDT/RSDT",
    "Live FPDT status=",
    "FIVE-STAGE LIVE DUT REPORT",
    "TreeView=ASCII hierarchy",
    "|-- [1/5] Collect DP raw records",
    "|-- FFS[",
    "|-- Section[",
    "UI Name=PciBusDxe source=FFS_UI_SECTION",
    "|-- NestedFV depth=",
    "Top[1] DurationNs=120000000",
    "[1/6] System and BIOS identification",
    "[2/5] Enumerate all firmware-visible FFS",
    "[3/5] Resolve GUID to module names",
    "[4/5] Performance records temporal tree",
    "[5/5] FV summary",
    "PerformanceTree source=FPDT_RAW_RECORDS",
    "DpX report complete:",
    "Live read-only FVB space accounting:",
    "NestedFV FFSCount=2 PAD=1",
    "Decompress=UEFI_STANDARD decodedBytes=4100; parent physical usage UNCHANGED",
    "Section[FV_IMAGE] depth=0 Type=0x17",
    "Accounting=CHILD_ONLY_DO_NOT_ADD_TO_PARENT",
    "No external firmware image was requested or loaded.",
    "GUID Resolver source=local FV2 FFS GUID",
    "GUID Resolver FFS scanned=",
    "Module=N/A reason=NO_VISIBLE_FFS_MATCH",
    "FunctionName=N/A reason=GUID_ALONE_HAS_NO_RVA_OR_SYMBOL_INFORMATION",
    "DPX Extended FPDT: Records=4 Events=2 Pairs=1",
    "DurationNs=120000000",
    "Module=PciBusDxe",
    "TriggerGuid=AAAAAAA0-0000-0000-0000-000000000001",
    "Token=PciEnumerator",
    "FunctionSymbol=UNVERIFIED_TOKEN",
    "RequestedGUID=93B80004-9FB3-11D4-9A3A-0090273FC14D",
    "UI=PciBusDxe",
)
missing = [s for s in required if s not in output]
if missing:
    raise SystemExit(f"OVMF Shell output lacked {missing}:\n{output}")
print("PASS: UEFI Shell output meets known-good and corrupt FV expectations")

if output.count("Live FPDT discovery source=firmware-published ACPI XSDT/RSDT") < 4:
    raise SystemExit("Both -perf-live and -perf-live -boot must execute in EFI Shell")
