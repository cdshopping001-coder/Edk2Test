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
  Print (L"  DpX.efi                 Five-stage LIVE DUT pipeline (no inputs)\n");
  Print (L"  DpX.efi -summary        Same local DUT survey\n");
  Print (L"  DpX.efi -help           Show help\n");
  Print (L"  DpX.efi -version        Show build version\n");
  Print (L"  DpX.efi -sysinfo        UEFI/CPU/SecureBoot information\n");
  Print (L"  DpX.efi -fv-list        Enumerate firmware-visible FV2 volumes\n");
  Print (L"  DpX.efi -fv-files -fv N List FV2 logical FFS with Sections\n");
  Print (L"  DpX.efi -fv-detail      Tree: FVB physical FFS/sections/nested FV\n");
  Print (L"  DpX.efi -guid-map       Match all FV2 FFS GUIDs to decoded UI module names\n");
  Print (L"  DpX.efi -guid-resolve <GUID> Resolve full DP GUID to on-board FFS module\n");
  Print (L"  DpX.efi -fv-space       Inspect local FVB raw Total/Used/Free (when readable)\n");
  Print (L"  DpX.efi -rom-analyze -input fs0:\\test.fv -fv-space\n");
  Print (L"  DpX.efi -rom-info       Probe FV2/FVB/SPI-NOR/HCI protocol handles\n");
  Print (L"  DpX.efi -rom-scan -input fs0:\\bios.bin  Scan standard independent FVs\n");
  Print (L"  DpX.efi -perf-parse -input fs0:\\BOOT.FBPT  Parse FPDT boot records\n");
  Print (L"  DpX.efi -perf-fpdt -input fs0:\\ACPI.FPDT  Validate ACPI FPDT pointers\n");
  Print (L"  DpX.efi -perf-live      Read this machine ACPI FPDT and safe FBPT\n");
  Print (L"  DpX.efi -perf-live -pointers  ACPI pointer records only\n");
  Print (L"\nM2 partial: single-FV at offset 0 and ROM scan of independent FV headers.\n");
  Print (L"Only logged FPDT event pairs are timed; MAP/PDB function resolution and platform SPI read are unavailable.\n");
  Print (L"Guided/private compression, Windows-only events and multi-boot sessions remain conditional.\n");
  Print (L"File import is an OPTIONAL offline validation workflow.\n");
  Print (L"No firmware writes, erases, lock changes or automatic resets are performed.\n");
}

STATIC
EFI_STATUS
DpXRunLocalSurvey (
  VOID
  )
{
  EFI_STATUS Status;
  UINTN Available;
  UINTN Partial;

  Available = 0;
  Partial = 0;
  Print (L"DpX %s | FIVE-STAGE LIVE DUT REPORT | no input files\n", DPX_VERSION);
  Print (L"Only firmware-visible records/volumes are analyzed; no writes or reset\n");
  Print (L"TreeSemantics=timestamp-containment, not verified call hierarchy\n");

  Print (L"\n[1/5] Collect DP raw records (ACPI FPDT or EDKII fallback)\n");
  DpXDeferEventAnalysis (TRUE);
  Status = DpXInspectLiveFpdt (TRUE);
  if (EFI_ERROR (Status)) {
    ++Partial;
    Print (L"  RawRecords=N/A (%r); no invented DP measurements\n", Status);
  } else {
    ++Available;
  }

  Print (L"\n[2/5] Enumerate all firmware-visible FFS and decoded sections\n");
  Status = DpXListFirmwareVolumes (TRUE, FALSE, FALSE, 0);
  if (EFI_ERROR (Status)) {
    ++Partial;
    Print (L"  FFSInventory=PARTIAL/UNAVAILABLE (%r)\n", Status);
  } else {
    ++Available;
  }

  Print (L"\n[3/5] Resolve GUID to module names from FFS UI sections\n");
  Status = DpXResolveModuleGuid (NULL);
  if (EFI_ERROR (Status)) {
    ++Partial;
    Print (L"  ModuleMapping=PARTIAL/UNAVAILABLE (%r)\n", Status);
  } else {
    ++Available;
  }

  Print (L"\n[4/5] Performance records temporal tree (Start/End pairs)\n");
  Status = DpXPrintDeferredPerformanceTree ();
  if (EFI_ERROR (Status)) {
    ++Partial;
    Print (L"  PerformanceTree=PARTIAL/UNAVAILABLE (%r)\n", Status);
  } else {
    ++Available;
  }

  Print (L"\n[5/5] FV summary (physical Total/Used/Free via FVB)\n");
  Status = DpXInspectLiveFvbSpaces (FALSE);
  if (EFI_ERROR (Status)) {
    ++Partial;
    Print (L"  FvCapacity=N/A (%r); FV2 logical bytes are not physical free space\n",
           Status);
  } else {
    ++Available;
  }

  Print (L"\nDpX report complete: stagesAvailable=%u/5 partial=%u\n",
         (UINT32)Available, (UINT32)Partial);
  Print (L"Source=THIS_BOOT; No external firmware image was requested or loaded.\n");
  Print (L"FunctionSymbol=N/A without matching MAP/PDB/RVA; tokens are labels\n");
  Print (L"Nested FV=logical child; never double counted in parent physical FV\n");
  return Available == 0 ? EFI_NOT_FOUND : EFI_SUCCESS;
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
    return DpXRunLocalSurvey ();
  }

  Command = Parameters->Argv[1];
  if ((StrCmp (Command, L"-help") == 0) ||
      (StrCmp (Command, L"-?") == 0))
  {
    DpXPrintHelp ();
    return EFI_SUCCESS;
  }

  if (StrCmp (Command, L"-summary") == 0) {
    return Parameters->Argc == 2 ? DpXRunLocalSurvey () : EFI_INVALID_PARAMETER;
  }

  if (StrCmp (Command, L"-version") == 0) {
    Print (L"DpX %s\n", DPX_VERSION);
    return EFI_SUCCESS;
  }

  if (StrCmp (Command, L"-sysinfo") == 0) {
    return (Parameters->Argc == 2) ? DpXPrintSystemInfo () : EFI_INVALID_PARAMETER;
  }

  if (StrCmp (Command, L"-guid-map") == 0) {
    return Parameters->Argc == 2 ?
           DpXResolveModuleGuid (NULL) : EFI_INVALID_PARAMETER;
  }

  if (StrCmp (Command, L"-guid-resolve") == 0) {
    EFI_GUID SearchGuid;
    if ((Parameters->Argc != 3) ||
        (StrLen (Parameters->Argv[2]) != 36) ||
        RETURN_ERROR (StrToGuid (Parameters->Argv[2], &SearchGuid)))
    {
      Print (L"Usage: DpX.efi -guid-resolve <xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx>\n");
      return EFI_INVALID_PARAMETER;
    }

    return DpXResolveModuleGuid (&SearchGuid);
  }

  if (StrCmp (Command, L"-fv-list") == 0) {
    return (Parameters->Argc == 2) ?
      DpXListFirmwareVolumes (FALSE, FALSE, FALSE, 0) : EFI_INVALID_PARAMETER;
  }

  if (StrCmp (Command, L"-fv-space") == 0) {
    if (Parameters->Argc != 2) { return EFI_INVALID_PARAMETER; }
    DpXListFirmwareVolumes (FALSE, FALSE, FALSE, 0);
    return DpXInspectLiveFvbSpaces (FALSE);
  }

  if (StrCmp (Command, L"-fv-detail") == 0) {
    if (Parameters->Argc != 2) { return EFI_INVALID_PARAMETER; }
    Print (L"DpX FV Detail: FV2 logical catalog; FVB physical catalog (when accessible)\n");
    DpXListFirmwareVolumes (TRUE, FALSE, FALSE, 0);
    return DpXInspectLiveFvbSpaces (TRUE);
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
    if (Parameters->Argc == 2) {
      return DpXInspectLiveFpdt (TRUE);
    }

    if (Parameters->Argc == 3) {
      if (StrCmp (Parameters->Argv[2], L"-pointers") == 0) {
        return DpXInspectLiveFpdt (FALSE);
      }

      if (StrCmp (Parameters->Argv[2], L"-boot") == 0) {
        return DpXInspectLiveFpdt (TRUE);
      }
    }

    Print (L"Usage: DpX.efi -perf-live [-boot|-pointers]\n");
    return EFI_INVALID_PARAMETER;
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
