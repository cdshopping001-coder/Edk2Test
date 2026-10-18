/** @file
  Non-sensitive system information with explicit source labels.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <Guid/GlobalVariable.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC
VOID
PrintOneByteVariable (
  IN CHAR16  *Name
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  UINT8       Value;

  Size   = sizeof (Value);
  Value  = 0;
  Status = gRT->GetVariable (
                  Name,
                  &gEfiGlobalVariableGuid,
                  NULL,
                  &Size,
                  &Value
                  );
  if (EFI_ERROR (Status) || (Size != sizeof (Value))) {
    Print (L"  %s: unavailable (%r), source=UEFI variable\n", Name, Status);
    return;
  }

  if (Value > 1) {
    Print (L"  %s: invalid value, source=UEFI variable\n", Name);
    return;
  }

  Print (L"  %s: %u, source=UEFI variable\n", Name, (UINT32)Value);
}

EFI_STATUS
DpXPrintSystemInfo (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT16      BootCurrent;
  UINTN       Size;

  Print (L"DpX System Inventory (read-only)\n");
  Print (L"Firmware vendor: %s, source=UEFI system table (firmware-reported)\n",
         gST->FirmwareVendor != NULL ? gST->FirmwareVendor : L"N/A");
  Print (L"Firmware revision: 0x%08x, source=UEFI system table\n",
         gST->FirmwareRevision);
  Print (L"UEFI revision: 0x%08x, source=UEFI system table\n",
         gST->Hdr.Revision);

#if defined (MDE_CPU_X64) || defined (MDE_CPU_IA32)
  {
    UINT32  MaxLeaf;
    UINT32  Ebx;
    UINT32  Ecx;
    UINT32  Edx;
    UINT32  Eax1;
    CHAR8   Vendor[13];

    AsmCpuid (0, &MaxLeaf, &Ebx, &Ecx, &Edx);
    CopyMem (&Vendor[0], &Ebx, sizeof (Ebx));
    CopyMem (&Vendor[4], &Edx, sizeof (Edx));
    CopyMem (&Vendor[8], &Ecx, sizeof (Ecx));
    Vendor[12] = '\0';
    Print (L"CPU vendor: %a, source=CPUID (direct observation)\n", Vendor);
    if (MaxLeaf >= 1) {
      AsmCpuid (1, &Eax1, NULL, NULL, NULL);
      Print (L"CPUID signature: 0x%08x, source=CPUID\n", Eax1);
    }
  }
#endif

  Print (L"Security (do not export keys):\n");
  PrintOneByteVariable (EFI_SECURE_BOOT_MODE_NAME);
  PrintOneByteVariable (EFI_SETUP_MODE_NAME);

  BootCurrent = 0;
  Size        = sizeof (BootCurrent);
  Status      = gRT->GetVariable (
                       EFI_BOOT_CURRENT_VARIABLE_NAME,
                       &gEfiGlobalVariableGuid,
                       NULL,
                       &Size,
                       &BootCurrent
                       );
  if (!EFI_ERROR (Status) && (Size == sizeof (BootCurrent))) {
    Print (L"BootCurrent: 0x%04x, source=UEFI variable\n", BootCurrent);
  } else {
    Print (L"BootCurrent: unavailable (%r), source=UEFI variable\n", Status);
  }

  Print (L"Inventory limitation: SMBIOS/PCI/TCG2/ACPI detail is planned, not implemented.\n");
  return EFI_SUCCESS;
}
