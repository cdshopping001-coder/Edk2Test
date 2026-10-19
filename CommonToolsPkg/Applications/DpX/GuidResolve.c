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

typedef struct {
  EFI_GUID Guid;
  UINTN Matches;
  CHAR16 Ui[DPX_UI_CHAR_LIMIT + 1];
} DPX_MODULE_MAP_ROW;

STATIC DPX_MODULE_MAP_ROW *mDpXModuleIndex = NULL;
STATIC UINTN mDpXModuleIndexCount = 0;
STATIC BOOLEAN mDpXModuleIndexComplete = FALSE;

/**
  Separate capture catalog: preserve *each* enumerated FFS record, even when
  multiple volumes contain the same GUID. This is data acquisition, not a
  presentation or unique module-name resolution decision.
**/
typedef struct {
  EFI_GUID Guid;
  UINT32 FvIndex;
  UINT8 Type;
  UINT64 LogicalSize;
  BOOLEAN UiValid;
  CHAR16 Ui[DPX_UI_CHAR_LIMIT + 1];
} DPX_CAPTURED_FFS;

STATIC DPX_CAPTURED_FFS *mDpXCapturedFfs = NULL;
STATIC UINTN mDpXCapturedFfsCount = 0;
STATIC UINTN mDpXUiUnavailableCount = 0;
STATIC BOOLEAN mDpXCapturedFfsComplete = FALSE;

VOID
DpXPrintFfsCaptureCoverage (
  VOID
  )
{
  Print (L"FFS capture: records=%u UI_missing=%u integrity=%s source=FV2; physical-hidden/nested-only regions NOT IMPLIED\n",
         (UINT32)mDpXCapturedFfsCount,
         (UINT32)mDpXUiUnavailableCount,
         mDpXCapturedFfsComplete ? L"COMPLETE_WITHIN_VISIBLE_FV2" : L"PARTIAL");
}

STATIC
VOID
DpXCaptureFfs (
  IN CONST EFI_GUID *Guid,
  IN UINTN FvIndex,
  IN EFI_FV_FILETYPE Type,
  IN UINTN Size,
  IN CONST CHAR16 *Ui
  )
{
  DPX_CAPTURED_FFS *Record;
  if (mDpXCapturedFfs == NULL || Guid == NULL) {
    mDpXCapturedFfsComplete = FALSE;
    return;
  }
  if (mDpXCapturedFfsCount >= DPX_GUID_MAP_MAX_ROWS) {
    mDpXCapturedFfsComplete = FALSE;
    return;
  }

  Record = &mDpXCapturedFfs[mDpXCapturedFfsCount++];
  Record->Guid = *Guid;
  Record->FvIndex = (UINT32)FvIndex;
  Record->Type = (UINT8)Type;
  Record->LogicalSize = Size;
  Record->UiValid = Ui != NULL;
  if (Ui != NULL) {
    CopyMem (Record->Ui, Ui, sizeof (Record->Ui));
  } else {
    ++mDpXUiUnavailableCount;
  }
}


VOID
DpXReleaseModuleIndex (
  VOID
  )
{
  if (mDpXModuleIndex != NULL) {
    FreePool (mDpXModuleIndex);
  }
  mDpXModuleIndex = NULL;
  mDpXModuleIndexCount = 0;
  mDpXModuleIndexComplete = FALSE;
  if (mDpXCapturedFfs != NULL) {
    FreePool (mDpXCapturedFfs);
  }
  mDpXCapturedFfs = NULL;
  mDpXCapturedFfsCount = 0;
  mDpXUiUnavailableCount = 0;
  mDpXCapturedFfsComplete = FALSE;
}

STATIC
VOID
DpXIndexFfs (
  IN CONST EFI_GUID *Guid,
  IN CONST CHAR16 *Ui
  )
{
  UINTN I;

  if (mDpXModuleIndex == NULL || Guid == NULL) {
    return;
  }

  for (I = 0; I < mDpXModuleIndexCount; ++I) {
    if (CompareGuid (&mDpXModuleIndex[I].Guid, Guid)) {
      // A duplicate FFS GUID must be marked ambiguous even when names match.
      ++mDpXModuleIndex[I].Matches;
      mDpXModuleIndex[I].Ui[0] = 0;
      return;
    }
  }

  if (mDpXModuleIndexCount >= DPX_GUID_MAP_MAX_ROWS) {
    mDpXModuleIndexComplete = FALSE;
    return;
  }

  mDpXModuleIndex[mDpXModuleIndexCount].Guid = *Guid;
  mDpXModuleIndex[mDpXModuleIndexCount].Matches = 1;
  if (Ui != NULL) {
    CopyMem (mDpXModuleIndex[mDpXModuleIndexCount].Ui, Ui,
             sizeof (mDpXModuleIndex[mDpXModuleIndexCount].Ui));
  }
  ++mDpXModuleIndexCount;
}


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
  if (OnlyGuid == NULL) {
    DpXReleaseModuleIndex ();
    mDpXModuleIndex = AllocateZeroPool (
                         DPX_GUID_MAP_MAX_ROWS * sizeof (DPX_MODULE_MAP_ROW)
                         );
    if (mDpXModuleIndex == NULL) {
      Print (L"GUID map cache=N/A reason=OUT_OF_RESOURCES; lookup will rescan FV2\n");
    } else {
      mDpXModuleIndexComplete = TRUE;
    }
    mDpXCapturedFfs = AllocateZeroPool (
                        DPX_GUID_MAP_MAX_ROWS * sizeof (DPX_CAPTURED_FFS)
                        );
    mDpXCapturedFfsComplete = (mDpXCapturedFfs != NULL);
  }

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
      mDpXCapturedFfsComplete = FALSE;
      continue;
    }

    if (Fv->KeySize == 0 || Fv->KeySize > DPX_MAX_KEY_SIZE) {
      Print (L"  FV[%u] skipped: unsafe FV2 KeySize\n", (UINT32)Index);
      mDpXCapturedFfsComplete = FALSE;
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
        mDpXModuleIndexComplete = FALSE;
        mDpXCapturedFfsComplete = FALSE;
        break;
      }

      ++Scanned;
      if (OnlyGuid != NULL && !CompareGuid (OnlyGuid, &FfsGuid)) {
        continue;
      }

      if (Matched >= OutputLimit) {
        Truncated = TRUE;
        mDpXModuleIndexComplete = FALSE;
        mDpXCapturedFfsComplete = FALSE;
        break;
      }

      ZeroMem (UiName, sizeof (UiName));
      QueryStatus = GetFfsUiName (
                          Fv, &FfsGuid, UiName,
                          sizeof (UiName) / sizeof (UiName[0])
                          );
      if (OnlyGuid == NULL) {
        DpXIndexFfs (&FfsGuid,
                     QueryStatus == EFI_SUCCESS ? UiName : NULL);
        DpXCaptureFfs (&FfsGuid, Index, Type, LogicalSize,
                       QueryStatus == EFI_SUCCESS ? UiName : NULL);
      }

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
      mDpXModuleIndexComplete = FALSE;
      mDpXCapturedFfsComplete = FALSE;
    }
  }

  FreePool (Handles);
  Print (L"GUID Resolver FFS scanned=%u matches=%u\n",
         (UINT32)Scanned, (UINT32)Matched);
  if (OnlyGuid == NULL) {
    Print (L"  GUID module index unique=%u status=%s\n",
           (UINT32)mDpXModuleIndexCount,
           mDpXModuleIndexComplete ? L"COMPLETE_VISIBLE_FV2" : L"PARTIAL_FALLBACK_SCAN");
    DpXPrintFfsCaptureCoverage ();
  }
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

/**
  Resolve an FPDT module GUID quietly for top-N performance rows.
  Returns an exact FFS-GUID match count; never treats event/protocol GUIDs as
  a module identity without an actual FFS match.
**/
EFI_STATUS
DpXLookupModuleUi (
  IN CONST EFI_GUID *Guid,
  OUT CHAR16        *Ui,
  IN UINTN          Capacity,
  OUT UINTN         *Matches
  )
{
  EFI_STATUS Status;
  EFI_STATUS Step;
  EFI_HANDLE *Handles;
  EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv;
  VOID *Key;
  UINTN HandleCount;
  UINTN Index;
  UINTN FileIndex;
  EFI_GUID FfsGuid;
  EFI_FV_FILETYPE FileType;
  EFI_FV_FILE_ATTRIBUTES Attr;
  UINTN FileSize;
  CHAR16 FoundUi[DPX_UI_CHAR_LIMIT + 1];

  if (Guid == NULL || Ui == NULL || Matches == NULL ||
      Capacity < 2 || Capacity > DPX_UI_CHAR_LIMIT + 1) {
    return EFI_INVALID_PARAMETER;
  }

  *Matches = 0;
  ZeroMem (Ui, Capacity * sizeof (CHAR16));
  if (mDpXModuleIndex != NULL && mDpXModuleIndexComplete) {
    for (Index = 0; Index < mDpXModuleIndexCount; ++Index) {
      if (CompareGuid (&mDpXModuleIndex[Index].Guid, Guid)) {
        *Matches = mDpXModuleIndex[Index].Matches;
        if (*Matches == 1 && mDpXModuleIndex[Index].Ui[0] != 0) {
          StrnCpyS (Ui, Capacity, mDpXModuleIndex[Index].Ui, Capacity - 1);
        }

        return EFI_SUCCESS;
      }
    }

    return EFI_NOT_FOUND;
  }

  Handles = NULL;
  HandleCount = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol, &gEfiFirmwareVolume2ProtocolGuid,
                  NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (HandleCount > DPX_MAX_FV_HANDLES) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0; Index < HandleCount; ++Index) {
    Fv = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (
                         Handles[Index], &gEfiFirmwareVolume2ProtocolGuid,
                         (VOID **)&Fv)) ||
        Fv == NULL || Fv->KeySize == 0 || Fv->KeySize > DPX_MAX_KEY_SIZE) {
      continue;
    }

    Key = AllocateZeroPool (Fv->KeySize);
    if (Key == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }

    for (FileIndex = 0; FileIndex < DPX_MAX_FFS_RECORDS; ++FileIndex) {
      FileType = EFI_FV_FILETYPE_ALL;
      FileSize = 0;
      Attr = 0;
      Step = Fv->GetNextFile (
                     Fv, Key, &FileType, &FfsGuid, &Attr, &FileSize);
      if (Step == EFI_NOT_FOUND || EFI_ERROR (Step)) {
        break;
      }

      if (!CompareGuid (Guid, &FfsGuid)) {
        continue;
      }

      ++*Matches;
      if (*Matches == 1) {
        ZeroMem (FoundUi, sizeof (FoundUi));
        Step = GetFfsUiName (Fv, &FfsGuid, FoundUi,
                            sizeof (FoundUi) / sizeof (FoundUi[0]));
        if (!EFI_ERROR (Step)) {
          CopyMem (Ui, FoundUi, Capacity * sizeof (CHAR16));
        }
      } else {
        Ui[0] = 0; // Ambiguous GUID mapping.
      }
    }

    FreePool (Key);
  }

  FreePool (Handles);
  return EFI_ERROR (Status) ? Status : (*Matches == 0 ? EFI_NOT_FOUND : EFI_SUCCESS);
}
