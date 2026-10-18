/** @file
  Capability probe only; no flash cycles are issued.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Pi/PiFirmwareVolume.h>
#include <Protocol/FirmwareVolumeBlock.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/SpiHc.h>
#include <Protocol/SpiNorFlash.h>

STATIC
VOID
PrintHandleCount (
  IN EFI_GUID  *ProtocolGuid,
  IN CHAR16    *Label
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       Count;

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (ByProtocol, ProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    Print (L"%s: unavailable (%r)\n", Label, Status);
    return;
  }

  Print (L"%s: %u handle(s) present; read authorization NOT established\n",
         Label, (UINT32)Count);
  FreePool (Handles);
}

EFI_STATUS
DpXPrintRomInfo (
  VOID
  )
{
  Print (L"DpX ROM/FV capabilities - protocol presence ONLY\n");
  PrintHandleCount (&gEfiFirmwareVolume2ProtocolGuid, L"FirmwareVolume2");
  PrintHandleCount (&gEfiFirmwareVolumeBlockProtocolGuid, L"FirmwareVolumeBlock");
  PrintHandleCount (&gEfiSpiHcProtocolGuid, L"PI SPI HostController");
  PrintHandleCount (&gEfiSpiNorFlashProtocolGuid, L"PI SPI NOR Flash");
  Print (L"Controller permissions / Descriptor / BIOS Region: UNKNOWN\n");
  Print (L"Raw SPI reads and ROM dump are NOT IMPLEMENTED; no flash writes attempted.\n");
  return EFI_SUCCESS;
}
