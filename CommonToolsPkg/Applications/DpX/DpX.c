/** @file
  Read-only DpX UEFI Shell command dispatcher.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/ShellParameters.h>

VOID
DpXPrintHelp (
  VOID
  )
{
  Print (L"DpX %s - UEFI firmware performance and inventory tools\n", DPX_VERSION);
  Print (L"  DpX.efi -help           Show help\n");
  Print (L"  DpX.efi -version        Show build version\n");
  Print (L"  DpX.efi -sysinfo        UEFI/CPU/SecureBoot information\n");
  Print (L"  DpX.efi -fv-list        Enumerate firmware-visible FV2 volumes\n");
  Print (L"  DpX.efi -fv-files -fv N List logical FFS files in FV index N\n");
  Print (L"  DpX.efi -fv-space       Live FV2 physical sizes remain N/A\n");
  Print (L"  DpX.efi -rom-analyze -input fs0:\\test.fv -fv-space\n");
  Print (L"  DpX.efi -rom-info       Probe FV2/FVB/SPI-NOR/HCI protocol handles\n");
  Print (L"  DpX.efi -rom-scan -input fs0:\\bios.bin  Scan standard independent FVs\n");
  Print (L"  DpX.efi -perf-parse -input fs0:\\BOOT.FBPT  Parse FPDT boot records\n");
  Print (L"  DpX.efi -perf-fpdt -input fs0:\\ACPI.FPDT  Validate ACPI FPDT pointers\n");
  Print (L"  DpX.efi -perf-live      Inspect firmware ACPI FPDT (no pointer reads)\n");
  Print (L"\nM2 partial: single-FV at offset 0 and ROM scan of independent FV headers.\n");
  Print (L"Live FPDT, full DP pairing, SPI dump, N-boot, nested FV: NOT IMPLEMENTED.\n");
  Print (L"No firmware writes, erases, lock changes or automatic resets are performed.\n");
}

STATIC
BOOLEAN
ParseIndex (
  IN  CONST CHAR16  *Text,
  OUT UINTN         *Index
  )
{
  UINTN  Value;
  UINTN  Digit;

  if ((Text == NULL) || (*Text == L'\0') || (Index == NULL)) {
    return FALSE;
  }

  Value = 0;
  while (*Text != L'\0') {
    if ((*Text < L'0') || (*Text > L'9')) {
      return FALSE;
    }

    Digit = (UINTN)(*Text - L'0');
    if (Value > ((MAX_UINTN - Digit) / 10)) {
      return FALSE;
    }

    Value = (Value * 10) + Digit;
    ++Text;
  }

  *Index = Value;
  return TRUE;
}

EFI_STATUS
EFIAPI
DpXEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_SHELL_PARAMETERS_PROTOCOL  *Parameters;
  CHAR16                         *Command;
  UINTN                          Index;

  (VOID)SystemTable;
  Parameters = NULL;
  Status     = gBS->HandleProtocol (
                      ImageHandle,
                      &gEfiShellParametersProtocolGuid,
                      (VOID **)&Parameters
                      );
  if (EFI_ERROR (Status) || (Parameters == NULL)) {
    Print (L"DpX: requires UEFI Shell 2.x parameters protocol (%r)\n", Status);
    return EFI_UNSUPPORTED;
  }

  if (Parameters->Argc == 1) {
    DpXPrintHelp ();
    return EFI_SUCCESS;
  }

  Command = Parameters->Argv[1];
  if ((StrCmp (Command, L"-help") == 0) ||
      (StrCmp (Command, L"-?") == 0))
  {
    DpXPrintHelp ();
    return EFI_SUCCESS;
  }

  if (StrCmp (Command, L"-version") == 0) {
    Print (L"DpX %s\n", DPX_VERSION);
    return EFI_SUCCESS;
  }

  if (StrCmp (Command, L"-sysinfo") == 0) {
    return (Parameters->Argc == 2) ? DpXPrintSystemInfo () : EFI_INVALID_PARAMETER;
  }

  if (StrCmp (Command, L"-fv-list") == 0) {
    return (Parameters->Argc == 2) ?
      DpXListFirmwareVolumes (FALSE, FALSE, FALSE, 0) : EFI_INVALID_PARAMETER;
  }

  if (StrCmp (Command, L"-fv-space") == 0) {
    return (Parameters->Argc == 2) ?
      DpXListFirmwareVolumes (FALSE, TRUE, FALSE, 0) : EFI_INVALID_PARAMETER;
  }

  if (StrCmp (Command, L"-fv-files") == 0) {
    if ((Parameters->Argc != 4) ||
        (StrCmp (Parameters->Argv[2], L"-fv") != 0) ||
        !ParseIndex (Parameters->Argv[3], &Index))
    {
      Print (L"Usage: DpX.efi -fv-files -fv N\n");
      return EFI_INVALID_PARAMETER;
    }

    return DpXListFirmwareVolumes (TRUE, FALSE, TRUE, Index);
  }

  if (StrCmp (Command, L"-rom-scan") == 0) {
    if ((Parameters->Argc != 4) ||
        (StrCmp (Parameters->Argv[2], L"-input") != 0))
    {
      Print (L"Usage: DpX.efi -rom-scan -input fsX:\\BIOS.rom\n");
      return EFI_INVALID_PARAMETER;
    }
    return DpXScanRawFirmwareFile (Parameters->Argv[3]);
  }

  if (StrCmp (Command, L"-rom-analyze") == 0) {
    if (((Parameters->Argc != 4) && (Parameters->Argc != 5)) ||
        (StrCmp (Parameters->Argv[2], L"-input") != 0) ||
        ((Parameters->Argc == 5) &&
         (StrCmp (Parameters->Argv[4], L"-fv-space") != 0)))
    {
      Print (L"Usage: DpX.efi -rom-analyze -input fsX:\\file.fv [-fv-space]\n");
      return EFI_INVALID_PARAMETER;
    }
    return DpXAnalyzeRawFirmwareFile (Parameters->Argv[3]);
  }

  if (StrCmp (Command, L"-perf-live") == 0) {
    return (Parameters->Argc == 2) ? DpXInspectLiveFpdt () : EFI_INVALID_PARAMETER;
  }

  if (StrCmp (Command, L"-perf-fpdt") == 0) {
    if ((Parameters->Argc != 4) ||
        (StrCmp (Parameters->Argv[2], L"-input") != 0))
    {
      Print (L"Usage: DpX.efi -perf-fpdt -input fsX:\\ACPI.FPDT\n");
      return EFI_INVALID_PARAMETER;
    }

    return DpXParseFpdtAcpiFile (Parameters->Argv[3]);
  }

  if (StrCmp (Command, L"-perf-parse") == 0) {
    if ((Parameters->Argc != 4) ||
        (StrCmp (Parameters->Argv[2], L"-input") != 0))
    {
      Print (L"Usage: DpX.efi -perf-parse -input fsX:\\BOOT.FBPT\n");
      return EFI_INVALID_PARAMETER;
    }

    return DpXParseFbptFile (Parameters->Argv[3]);
  }

  if (StrCmp (Command, L"-rom-info") == 0) {
    return (Parameters->Argc == 2) ? DpXPrintRomInfo () : EFI_INVALID_PARAMETER;
  }

  Print (L"Unsupported or unknown DpX command: %s\n", Command);
  Print (L"Use -help to list implemented commands.\n");
  return EFI_UNSUPPORTED;
}
