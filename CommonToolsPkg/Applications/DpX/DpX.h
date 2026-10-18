/** @file
  DpX v0.1 Shell application contract.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef DPX_H_
#define DPX_H_

#include <Uefi.h>

#define DPX_VERSION         L"0.1.0"
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

#endif
