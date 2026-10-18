/** @file
  DpX v0.6 Shell application contract.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef DPX_H_
#define DPX_H_

#include <Uefi.h>

#define DPX_VERSION         L"0.6.0"
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
  VOID
  );

#endif
