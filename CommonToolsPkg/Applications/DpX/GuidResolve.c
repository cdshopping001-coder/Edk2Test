/** @file
  DpX on-device FFS GUID -> module-name resolver.

  Only firmware-published FV2 protocols are consulted. ReadSection handles
  decompression / guided extraction when installed and supported by firmware.
  Merely finding an FFS GUID does not establish a source-level function name.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Pi/PiFirmwareFile.h>
#include <Pi/PiFirmwareVolume.h>
#include <Protocol/FirmwareVolume2.h>

#define DPX_UI_CHAR_LIMIT     127
#define DPX_GUID_MAP_MAX_ROWS 4096

STATIC
EFI_STATUS
GetFfsUiName (
  IN  EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv,
  IN  CONST EFI_GUID               *Name,
  OUT CHAR16                       *Ui,
  IN  UINTN                        UiChars
  )
{
  EFI_STATUS Status;
  VOID       *Buffer;
  UINTN      Size;
  UINT32     Auth;
  UINTN      Index;
  CHAR16     *Text;

  if (Fv == NULL || Name == NULL || Ui == NULL || UiChars < 2 ||
      UiChars > DPX_UI_CHAR_LIMIT + 1)
  {
    return EFI_INVALID_PARAMETER;
  }

  // Pass a fixed caller-owned buffer. Never let firmware allocate an
  // unbounded output for a UI section while resolving a log GUID.
  ZeroMem (Ui, UiChars * sizeof (CHAR16));
  Buffer = Ui;
  Size = (UiChars - 1) * sizeof (CHAR16);
  Auth = 0;
  Status = Fv->ReadSection (
                 Fv,
                 Name,
                 EFI_SECTION_USER_INTERFACE,
                 0,
                 &Buffer,
                 &Size,
                 &Auth
                 );
  if (Status != EFI_SUCCESS || Buffer != Ui ||
      (Size < sizeof (CHAR16)) || ((Size & 1) != 0) ||
      Size > (UiChars - 1) * sizeof (CHAR16))
  {
    return (Status == EFI_SUCCESS) ? EFI_COMPROMISED_DATA : Status;
  }

  Text = (CHAR16 *)Buffer;
  for (Index = 0; Index < Size / sizeof (CHAR16); ++Index) {
    if (Text[Index] == 0) {
      return (Index == 0) ? EFI_NOT_FOUND : EFI_SUCCESS;
    }

    // Avoid terminal escapes and line-break injection from malformed images.
    if (Text[Index] < 0x20 || Text[Index] == 0x7f) {
      Text[Index] = L'?';
    }
  }

  // An unterminated UI section is not a trustworthy module name.
  Ui[0] = 0;
  return EFI_COMPROMISED_DATA;
}

EFI_STATUS
DpXResolveModuleGuid (
  IN CONST EFI_GUID *OnlyGuid OPTIONAL
  )
{
  EFI_STATUS                     Status;
  EFI_STATUS                     QueryStatus;
  EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv;
  EFI_HANDLE                     *Handles;
  VOID                           *Key;
  UINTN                          HandlesCount;
  UINTN                          Index;
  UINTN                          FileIndex;
  UINTN                          Scanned;
  UINTN                          Matched;
  EFI_GUID                       FfsGuid;
  EFI_FV_FILETYPE                Type;
  EFI_FV_FILE_ATTRIBUTES         Attr;
  UINTN                          LogicalSize;
  CHAR16                         UiName[DPX_UI_CHAR_LIMIT + 1];
  UINTN                          OutputLimit;
  BOOLEAN                        Truncated;

  Handles = NULL;
  HandlesCount = 0;
  Scanned = 0;
  Matched = 0;
  Truncated = FALSE;
  OutputLimit = OnlyGuid == NULL ? DPX_GUID_MAP_MAX_ROWS : MAX_UINTN;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol, &gEfiFirmwareVolume2ProtocolGuid,
                  NULL, &HandlesCount, &Handles
                  );
  if (EFI_ERROR (Status)) {
    Print (L"GUID Resolver: FV2 not available (%r); module=N/A\n", Status);
    return Status;
  }

  if (HandlesCount > DPX_MAX_FV_HANDLES) {
    Print (L"GUID Resolver: too many FV2 handles (%u)\n", (UINT32)HandlesCount);
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Print (L"GUID Resolver source=local FV2 FFS GUID, UI=ReadSection (nested decompression if supported)\n");
  if (OnlyGuid != NULL) {
    Print (L"  RequestedGUID=%g\n", OnlyGuid);
  }

  Status = EFI_SUCCESS;
  for (Index = 0; Index < HandlesCount && !Truncated; ++Index) {
    Fv = NULL;
    QueryStatus = gBS->HandleProtocol (
                        Handles[Index], &gEfiFirmwareVolume2ProtocolGuid,
                        (VOID **)&Fv
                        );
    if (EFI_ERROR (QueryStatus) || Fv == NULL) {
      continue;
    }

    if (Fv->KeySize == 0 || Fv->KeySize > DPX_MAX_KEY_SIZE) {
      Print (L"  FV[%u] skipped: unsafe FV2 KeySize\n", (UINT32)Index);
      continue;
    }

    Key = AllocateZeroPool (Fv->KeySize);
    if (Key == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }

    for (FileIndex = 0; FileIndex < DPX_MAX_FFS_RECORDS; ++FileIndex) {
      Type = EFI_FV_FILETYPE_ALL;
      Attr = 0;
      LogicalSize = 0;
      ZeroMem (&FfsGuid, sizeof (FfsGuid));
      QueryStatus = Fv->GetNextFile (
                          Fv, Key, &Type, &FfsGuid,
                          &Attr, &LogicalSize
                          );
      if (QueryStatus == EFI_NOT_FOUND) {
        break;
      }

      if (EFI_ERROR (QueryStatus)) {
        Print (L"  FV[%u]: FFS enumeration partial (%r)\n",
               (UINT32)Index, QueryStatus);
        break;
      }

      ++Scanned;
      if (OnlyGuid != NULL && !CompareGuid (OnlyGuid, &FfsGuid)) {
        continue;
      }

      if (Matched >= OutputLimit) {
        Truncated = TRUE;
        break;
      }

      ZeroMem (UiName, sizeof (UiName));
      QueryStatus = GetFfsUiName (
                          Fv, &FfsGuid, UiName,
                          sizeof (UiName) / sizeof (UiName[0])
                          );
      Print (L"  FV[%u] FFS=%g Type=0x%02x LogicalSize=%lu UI=%s\n",
             (UINT32)Index, &FfsGuid, (UINT32)Type,
             (UINT64)LogicalSize,
             (QueryStatus == EFI_SUCCESS) ? UiName : L"N/A");
      if (QueryStatus != EFI_SUCCESS) {
        Print (L"    UiSection=N/A (%r); compressed/guided sections may be unsupported\n",
               QueryStatus);
      }

      ++Matched;
    }

    FreePool (Key);
    if (FileIndex == DPX_MAX_FFS_RECORDS) {
      Print (L"  FV[%u] FFS bounded; mapping may be incomplete\n", (UINT32)Index);
    }
  }

  FreePool (Handles);
  Print (L"GUID Resolver FFS scanned=%u matches=%u\n",
         (UINT32)Scanned, (UINT32)Matched);
  if (Truncated) {
    Print (L"GUID Resolver status=partial reason=OUTPUT_ROW_LIMIT\n");
  }

  if (OnlyGuid != NULL && Matched == 0) {
    Print (L"  Module=N/A reason=NO_VISIBLE_FFS_MATCH\n");
    Print (L"  The GUID may identify a Protocol/PPI/Event, hidden FV, or a caller not represented by FFS\n");
    Print (L"  FunctionName=N/A reason=GUID_ALONE_HAS_NO_RVA_OR_SYMBOL_INFORMATION\n");
    return EFI_NOT_FOUND;
  }

  if (OnlyGuid != NULL) {
    Print (L"  FunctionName=N/A reason=GUID_ALONE_HAS_NO_RVA_OR_SYMBOL_INFORMATION\n");
    if (Matched > 1) {
      Print (L"  Confidence=AMBIGUOUS multiple FFS with the same GUID\n");
    } else {
      Print (L"  Confidence=FFS_GUID_MATCH (not proof of executing callback)\n");
    }
  }

  return Status;
}
