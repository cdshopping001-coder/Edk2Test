/** @file
  DpX v0.14 Shell application contract.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef DPX_H_
#define DPX_H_

#include <Uefi.h>

#define DPX_VERSION         L"0.14.0"
#define DPX_MAX_FV_HANDLES  1024
#define DPX_MAX_FFS_RECORDS 32768
#define DPX_MAX_KEY_SIZE    4096

VOID
DpXPrintHelp (
  VOID
  );

EFI_STATUS
DpXPrintSystemInfo (
  VOID
  );

EFI_STATUS
DpXListFirmwareVolumes (
  IN BOOLEAN  ShowFiles,
  IN BOOLEAN  SpaceReport,
  IN BOOLEAN  FilterByIndex,
  IN UINTN    RequestedIndex
  );

EFI_STATUS
DpXInspectLiveFvbSpaces (
  IN BOOLEAN Details
  );

EFI_STATUS
DpXResolveModuleGuid (
  IN CONST EFI_GUID *OnlyGuid OPTIONAL
  );

EFI_STATUS
DpXLookupModuleUi (
  IN CONST EFI_GUID *Guid,
  OUT CHAR16 *Ui,
  IN UINTN Capacity,
  OUT UINTN *Matches
  );

VOID
DpXDeferEventAnalysis (
  IN BOOLEAN Enable
  );

EFI_STATUS
DpXPrintDeferredPerformanceTree (
  VOID
  );

EFI_STATUS
DpXAnalyzeExtendedFbpt (
  IN CONST UINT8 *Data,
  IN UINTN Available
  );

EFI_STATUS
DpXPrintRomInfo (
  VOID
  );

EFI_STATUS
DpXAnalyzeRawFirmwareFile (
  IN CONST CHAR16 *Path
  );

EFI_STATUS
DpXScanRawFirmwareFile (
  IN CONST CHAR16 *Path
  );

EFI_STATUS
DpXParseFbptFile (
  IN CONST CHAR16 *Path
  );

EFI_STATUS
DpXParseFpdtAcpiFile (
  IN CONST CHAR16 *Path
  );

EFI_STATUS
DpXInspectLiveFpdt (
  IN BOOLEAN ReadBoot
  );

EFI_STATUS
DpXReadLiveFbpt (
  IN UINT64 BootPhysicalAddress
  );

/**
  EFI Shell-safe ASCII tree branch. Zero-based Level is nesting depth.
  Never relies on optional console box-drawing glyph support.
**/
VOID
DpXTreePrefix (
  IN UINTN Level,
  IN BOOLEAN IsLast
  );

EFI_STATUS
DpXInspectFfsSections (
  IN CONST UINT8 *Bytes,
  IN UINTN Size,
  IN BOOLEAN OffsetValid,
  IN UINT64 FvRelativeOffset
  );

#endif
