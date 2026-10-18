/** @file
  Logical FV2 inventory. Never interpret FV2 file sizes as raw FV free space.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Pi/PiFirmwareFile.h>
#include <Pi/PiFirmwareVolume.h>
#include <Protocol/FirmwareVolume2.h>

STATIC
EFI_STATUS
EnumerateFiles (
  IN EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv
  )
{
  EFI_STATUS              Status;
  VOID                    *Key;
  EFI_FV_FILETYPE         FileType;
  EFI_GUID                Name;
  EFI_FV_FILE_ATTRIBUTES  Attributes;
  UINTN                   FileSize;
  UINTN                   Count;
  VOID                    *Data;
  UINTN                   ReadBytes;
  EFI_FV_FILETYPE         FoundType;
  EFI_FV_FILE_ATTRIBUTES  FoundAttrs;
  UINT32                  Authentication;

  if ((Fv->KeySize == 0) || (Fv->KeySize > DPX_MAX_KEY_SIZE)) {
    Print (L"  FFS listing unavailable: unsafe/invalid KeySize=%u\n", (UINT32)Fv->KeySize);
    return EFI_UNSUPPORTED;
  }

  Key = AllocateZeroPool (Fv->KeySize);
  if (Key == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  for (Count = 0; Count < DPX_MAX_FFS_RECORDS; ++Count) {
    FileType   = EFI_FV_FILETYPE_ALL;
    Attributes = 0;
    FileSize   = 0;
    ZeroMem (&Name, sizeof (Name));
    Status = Fv->GetNextFile (Fv, Key, &FileType, &Name, &Attributes, &FileSize);
    if (Status == EFI_NOT_FOUND) {
      break;
    }

    if (EFI_ERROR (Status)) {
      Print (L"  FFS enumeration stopped: %r after %u entries\n", Status, (UINT32)Count);
      FreePool (Key);
      return Status;
    }

    Print (L"  [%u] GUID=%g Type=0x%02x LogicalSize=%lu Attr=0x%08x Offset=N/A (FV2 logical view)\n",
           (UINT32)Count, &Name, (UINT32)FileType,
           (UINT64)FileSize, (UINT32)Attributes);
    if (FileType != EFI_FV_FILETYPE_FFS_PAD) {
      if ((FileSize == 0) || (FileSize > 4 * 1024 * 1024)) {
        Print (L"    SectionDetail=N/A reason=LOGICAL_FILE_SIZE_LIMIT\n");
        continue;
      }

      Data = AllocatePool (FileSize);
      if (Data == NULL) {
        Print (L"    SectionDetail=N/A reason=OUT_OF_RESOURCES\n");
        continue;
      }

      ReadBytes = FileSize;
      FoundType = 0;
      FoundAttrs = 0;
      Authentication = 0;
      Status = Fv->ReadFile (
                     Fv, &Name, &Data, &ReadBytes,
                     &FoundType, &FoundAttrs, &Authentication
                     );
      if (Status == EFI_SUCCESS && ReadBytes <= FileSize) {
        DpXInspectFfsSections ((CONST UINT8 *)Data, ReadBytes, FALSE, 0);
      } else {
        Print (L"    SectionDetail=N/A reason=FV2_READFILE_FAILED (%r)\n", Status);
      }

      FreePool (Data);
    }
  }

  FreePool (Key);
  if (Count == DPX_MAX_FFS_RECORDS) {
    Print (L"  FFS enumeration bounded at %u entries; result partial\n", (UINT32)DPX_MAX_FFS_RECORDS);
    return EFI_BUFFER_TOO_SMALL;
  }

  Print (L"  Logical FFS file count: %u; physical FV used/free NOT derived\n", (UINT32)Count);
  return EFI_SUCCESS;
}

EFI_STATUS
DpXListFirmwareVolumes (
  IN BOOLEAN  ShowFiles,
  IN BOOLEAN  SpaceReport,
  IN BOOLEAN  FilterByIndex,
  IN UINTN    RequestedIndex
  )
{
  EFI_STATUS                     Status;
  EFI_STATUS                     Result;
  EFI_HANDLE                     *Handles;
  UINTN                          Count;
  UINTN                          Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv;
  EFI_FV_ATTRIBUTES              Attributes;

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiFirmwareVolume2ProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (Status == EFI_NOT_FOUND) {
    Print (L"No FV2 protocol handles: source=FV2, status=unsupported\n");
    return EFI_NOT_FOUND;
  }

  if (EFI_ERROR (Status)) {
    Print (L"FV2 handle enumeration failed: %r\n", Status);
    return Status;
  }

  if (Count > DPX_MAX_FV_HANDLES) {
    Print (L"Too many FV2 handles (%u), refusing unbounded enumeration\n", (UINT32)Count);
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  if (FilterByIndex && (RequestedIndex >= Count)) {
    Print (L"FV index %u out of range; count=%u\n", (UINT32)RequestedIndex, (UINT32)Count);
    FreePool (Handles);
    return EFI_INVALID_PARAMETER;
  }

  Print (L"FV2 logical inventory: %u visible handle(s)\n", (UINT32)Count);
  Result = EFI_SUCCESS;

  for (Index = 0; Index < Count; ++Index) {
    if (FilterByIndex && (Index != RequestedIndex)) {
      continue;
    }

    Fv     = NULL;
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiFirmwareVolume2ProtocolGuid,
                    (VOID **)&Fv
                    );
    if (EFI_ERROR (Status) || (Fv == NULL)) {
      Print (L"FV[%u]: unable to access FV2 (%r)\n", (UINT32)Index, Status);
      Result = EFI_DEVICE_ERROR;
      continue;
    }

    Attributes = 0;
    Status     = Fv->GetVolumeAttributes (Fv, &Attributes);
    if (EFI_ERROR (Status)) {
      Print (L"FV[%u]: attributes unavailable (%r)\n", (UINT32)Index, Status);
    } else {
      Print (L"FV[%u]: FV2 Attributes=0x%lx read=%s\n",
             (UINT32)Index, Attributes,
             (Attributes & EFI_FV2_READ_STATUS) != 0 ? L"enabled" : L"disabled");
    }

    if (SpaceReport) {
      Print (L"FV[%u]: Total=N/A Used=N/A Free=N/A Usage=N/A\n", (UINT32)Index);
      Print (L"  source=FV2 logical only, reason=RAW_FV_NOT_ACCESSIBLE (not yet collected)\n");
    }

    if (ShowFiles) {
      Status = EnumerateFiles (Fv);
      if (EFI_ERROR (Status)) {
        Result = Status;
      }
    }
  }

  FreePool (Handles);
  return Result;
}
