/** @file
  Live read-only byte accounting of PI firmware volumes through EFI_FVB2.
  Never guesses Used/Free from FV2 logical file sizes.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"
#include <Guid/FirmwareFileSystem2.h>
#include <Guid/FirmwareFileSystem3.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Pi/PiFirmwareFile.h>
#include <Pi/PiFirmwareVolume.h>
#include <Protocol/FirmwareVolumeBlock.h>

#define DPX_LIVE_FV_LIMIT       (128ULL * 1024 * 1024)
#define DPX_LIVE_HEADER_LIMIT   (64 * 1024)
#define DPX_LIVE_FVB_HANDLES    256
#define DPX_LIVE_GEOMETRY_RUNS  4096
#define DPX_LIVE_READ_CHUNK     4096

/**
  Locate a byte offset using FVB's LBA geometry and then read only from
  the returned block. Never assume that all flash erase blocks have one size.
**/
STATIC
EFI_STATUS
ReadFvbBytes (
  IN EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *Fvb,
  IN UINT64                               At,
  OUT VOID                                 *Target,
  IN UINTN                                 Bytes
  )
{
  EFI_STATUS  Status;
  UINT64      Remaining;
  EFI_LBA     Lba;
  UINTN       BlockSize;
  UINTN       Count;
  UINT64      Span;
  UINTN       Offset;
  UINTN       Actual;
  UINTN       Part;
  UINTN       Run;
  UINT8       *Destination;

  Destination = (UINT8 *)Target;
  while (Bytes != 0) {
    Remaining = At;
    Lba = 0;
    BlockSize = 0;
    Count = 0;
    for (Run = 0; Run < DPX_LIVE_GEOMETRY_RUNS; ++Run) {
      Status = Fvb->GetBlockSize (Fvb, Lba, &BlockSize, &Count);
      if (EFI_ERROR (Status) || BlockSize == 0 || Count == 0 ||
          (Count > MAX_UINT64 / BlockSize))
      {
        return EFI_DEVICE_ERROR;
      }

      Span = (UINT64)Count * BlockSize;
      if (Remaining < Span) {
        break;
      }

      Remaining -= Span;
      if (Lba > MAX_UINT64 - Count) {
        return EFI_VOLUME_CORRUPTED;
      }

      Lba += Count;
    }

    if (Run == DPX_LIVE_GEOMETRY_RUNS) {
      return EFI_UNSUPPORTED;
    }

    Lba += Remaining / BlockSize;
    Offset = (UINTN)(Remaining % BlockSize);
    Part = BlockSize - Offset;
    if (Part > Bytes) {
      Part = Bytes;
    }

    Actual = Part;
    Status = Fvb->Read (Fvb, Lba, Offset, &Actual, Destination);
    if (EFI_ERROR (Status) || Actual != Part) {
      return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
    }

    Bytes -= Part;
    Destination += Part;
    if (At > MAX_UINT64 - Part) {
      return EFI_VOLUME_CORRUPTED;
    }

    At += Part;
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
AllErase (
  IN CONST UINT8  *Data,
  IN UINTN        Size,
  IN UINT8        Erase
  )
{
  UINTN Index;
  for (Index = 0; Index < Size; ++Index) {
    if (Data[Index] != Erase) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC
EFI_STATUS
CheckTail (
  IN EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *Fvb,
  IN UINT64                              At,
  IN UINT64                              Last,
  IN UINT8                               Erase
  )
{
  UINT8 Buffer[DPX_LIVE_READ_CHUNK];
  UINTN Count;
  EFI_STATUS Status;
  while (At < Last) {
    Count = (UINTN)(((Last - At) < sizeof (Buffer)) ?
                    Last - At : sizeof (Buffer));
    Status = ReadFvbBytes (Fvb, At, Buffer, Count);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (!AllErase (Buffer, Count, Erase)) {
      return EFI_VOLUME_CORRUPTED;
    }

    At += Count;
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
HeaderChecksumValid (
  IN CONST UINT8 *Header,
  IN UINTN       HeaderLength
  )
{
  UINT16 Sum;
  UINTN  I;
  Sum = 0;
  for (I = 0; I < HeaderLength; I += 2) {
    Sum = (UINT16)(Sum + Header[I] + ((UINT16)Header[I + 1] << 8));
  }

  return Sum == 0;
}

STATIC
EFI_STATUS
ValidateGeometry (
  IN CONST UINT8 *Raw,
  IN UINTN       HeaderLength,
  IN UINT64      Total
  )
{
  EFI_FV_BLOCK_MAP_ENTRY  Entry;
  UINTN Offset;
  UINT64 Sum;
  UINT64 Added;
  BOOLEAN End;
  End = FALSE;
  Sum = 0;
  for (Offset = OFFSET_OF (EFI_FIRMWARE_VOLUME_HEADER, BlockMap);
       Offset + sizeof (Entry) <= HeaderLength;
       Offset += sizeof (Entry))
  {
    CopyMem (&Entry, Raw + Offset, sizeof (Entry));
    if (Entry.NumBlocks == 0 && Entry.Length == 0) {
      End = TRUE;
      break;
    }

    if (Entry.NumBlocks == 0 || Entry.Length == 0) {
      return EFI_VOLUME_CORRUPTED;
    }

    Added = (UINT64)Entry.NumBlocks * Entry.Length;
    if (Sum > Total || Added > Total - Sum) {
      return EFI_VOLUME_CORRUPTED;
    }

    Sum += Added;
  }

  return (End && Sum == Total) ? EFI_SUCCESS : EFI_VOLUME_CORRUPTED;
}

STATIC
UINT8
HighestState (
  IN UINT8 State,
  IN UINT8 Erase
  )
{
  UINT8 Bit;
  if (Erase == 0xFF) {
    State = (UINT8)~State;
  }

  for (Bit = 0x80; Bit != 0; Bit >>= 1) {
    if ((State & Bit) != 0) {
      return Bit;
    }
  }

  return 0;
}

STATIC
BOOLEAN
FileChecksumValid (
  IN CONST UINT8 *Raw,
  IN UINTN       Size
  )
{
  UINTN I;
  UINT8 Sum;
  CONST EFI_FFS_FILE_HEADER *Ffs;
  Ffs = (CONST EFI_FFS_FILE_HEADER *)Raw;
  Sum = 0;
  for (I = 0; I < Size; ++I) {
    Sum = (UINT8)(Sum + Raw[I]);
  }

  return (UINT8)(Sum - Ffs->IntegrityCheck.Checksum.File - Ffs->State) == 0;
}

STATIC
EFI_STATUS
InspectOneFvb (
  IN EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *Fvb,
  IN UINTN                               Index
  )
{
  EFI_STATUS Status;
  EFI_FVB_ATTRIBUTES_2 Attr;
  EFI_FIRMWARE_VOLUME_HEADER Fv;
  EFI_FIRMWARE_VOLUME_EXT_HEADER Ext;
  EFI_FFS_FILE_HEADER Ffs;
  UINT8 FfsRaw[sizeof (EFI_FFS_FILE_HEADER2)];
  UINT8 *Raw;
  UINT8 Erase;
  UINT64 Start;
  UINT64 Offset;
  UINT64 Size;
  UINT64 Aligned;
  UINT64 Free;
  UINT64 Percentage;
  UINT32 Size24;
  UINTN HeaderSize;
  UINTN I;
  UINT32 Count;
  UINT32 Pad;
  BOOLEAN IsValid;

  Print (L"  LiveFVB[%u]: ", (UINT32)Index);
  Attr = 0;
  Status = Fvb->GetAttributes (Fvb, &Attr);
  if (EFI_ERROR (Status) || (Attr & EFI_FVB2_READ_STATUS) == 0) {
    Print (L"Total=N/A Used=N/A Free=N/A reason=FVB_READ_DISABLED (%r)\n", Status);
    return EFI_ACCESS_DENIED;
  }

  Status = ReadFvbBytes (Fvb, 0, &Fv, sizeof (Fv));
  if (EFI_ERROR (Status)) {
    Print (L"Total=N/A Used=N/A Free=N/A reason=FVB_HEADER_READ_FAILED (%r)\n", Status);
    return Status;
  }

  if (Fv.Signature != EFI_FVH_SIGNATURE ||
      Fv.Revision != EFI_FVH_REVISION ||
      (!CompareGuid (&Fv.FileSystemGuid, &gEfiFirmwareFileSystem2Guid) &&
       !CompareGuid (&Fv.FileSystemGuid, &gEfiFirmwareFileSystem3Guid)))
  {
    Print (L"Total=N/A Used=N/A Free=N/A reason=NOT_FFS2_OR_FFS3_FV\n");
    return EFI_UNSUPPORTED;
  }

  if (Fv.FvLength == 0 || Fv.FvLength > DPX_LIVE_FV_LIMIT ||
      Fv.HeaderLength < sizeof (Fv) ||
      Fv.HeaderLength > DPX_LIVE_HEADER_LIMIT ||
      Fv.HeaderLength > Fv.FvLength ||
      (Fv.HeaderLength & 1) != 0)
  {
    Print (L"Total=N/A Used=N/A Free=N/A reason=INVALID_FV_LENGTH\n");
    return EFI_VOLUME_CORRUPTED;
  }

  Raw = AllocatePool (Fv.HeaderLength);
  if (Raw == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = ReadFvbBytes (Fvb, 0, Raw, Fv.HeaderLength);
  if (!EFI_ERROR (Status)) {
    if (!HeaderChecksumValid (Raw, Fv.HeaderLength)) {
      Status = EFI_CRC_ERROR;
    } else {
      Status = ValidateGeometry (Raw, Fv.HeaderLength, Fv.FvLength);
    }
  }

  FreePool (Raw);
  if (EFI_ERROR (Status)) {
    Print (L"Total=N/A Used=N/A Free=N/A reason=HEADER_OR_GEOMETRY_INVALID (%r)\n", Status);
    return Status;
  }

  Print (L"Total=%lu bytes source=verified FVB raw layout\n", Fv.FvLength);
  Erase = (Fv.Attributes & EFI_FVB2_ERASE_POLARITY) ? 0xFF : 0;
  Start = Fv.HeaderLength;
  if (Fv.ExtHeaderOffset != 0) {
    if (Fv.ExtHeaderOffset < Fv.HeaderLength ||
        Fv.ExtHeaderOffset > Fv.FvLength ||
        sizeof (Ext) > Fv.FvLength - Fv.ExtHeaderOffset)
    {
      Print (L"    Used=N/A Free=N/A reason=EXT_HEADER_BOUNDS\n");
      return EFI_VOLUME_CORRUPTED;
    }

    Status = ReadFvbBytes (Fvb, Fv.ExtHeaderOffset, &Ext, sizeof (Ext));
    if (EFI_ERROR (Status) || Ext.ExtHeaderSize < sizeof (Ext) ||
        Ext.ExtHeaderSize > Fv.FvLength - Fv.ExtHeaderOffset)
    {
      Print (L"    Used=N/A Free=N/A reason=EXT_HEADER_INVALID\n");
      return EFI_VOLUME_CORRUPTED;
    }

    Start = Fv.ExtHeaderOffset + Ext.ExtHeaderSize;
  }

  if (Start > Fv.FvLength - 7) {
    Print (L"    Used=N/A Free=N/A reason=FFS_START_OUT_OF_RANGE\n");
    return EFI_VOLUME_CORRUPTED;
  }

  Offset = (Start + 7) & ~7ULL;
  Count = 0;
  Pad = 0;
  Status = EFI_SUCCESS;

  while (Offset < Fv.FvLength) {
    if (Fv.FvLength - Offset < sizeof (Ffs)) {
      Status = CheckTail (Fvb, Offset, Fv.FvLength, Erase);
      break;
    }

    Status = ReadFvbBytes (Fvb, Offset, &Ffs, sizeof (Ffs));
    if (EFI_ERROR (Status)) {
      break;
    }

    if (AllErase ((CONST UINT8 *)&Ffs, sizeof (Ffs), Erase)) {
      Status = CheckTail (Fvb, Offset, Fv.FvLength, Erase);
      break;
    }

    if (HighestState (Ffs.State, Erase) != EFI_FILE_DATA_VALID &&
        HighestState (Ffs.State, Erase) != EFI_FILE_MARKED_FOR_UPDATE &&
        HighestState (Ffs.State, Erase) != EFI_FILE_DELETED)
    {
      Status = EFI_UNSUPPORTED;
      break;
    }

    HeaderSize = (Ffs.Attributes & FFS_ATTRIB_LARGE_FILE) ?
                 sizeof (EFI_FFS_FILE_HEADER2) : sizeof (EFI_FFS_FILE_HEADER);
    if (HeaderSize > Fv.FvLength - Offset) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    Status = ReadFvbBytes (Fvb, Offset, FfsRaw, HeaderSize);
    if (EFI_ERROR (Status)) {
      break;
    }

    if (!FileChecksumValid (FfsRaw, HeaderSize)) {
      Status = EFI_CRC_ERROR;
      break;
    }

    Size24 = (UINT32)Ffs.Size[0] |
             ((UINT32)Ffs.Size[1] << 8) |
             ((UINT32)Ffs.Size[2] << 16);
    if (Ffs.Attributes & FFS_ATTRIB_LARGE_FILE) {
      if (Size24 != 0) {
        Status = EFI_VOLUME_CORRUPTED;
        break;
      }

      CopyMem (&Size, FfsRaw + OFFSET_OF (EFI_FFS_FILE_HEADER2, ExtendedSize), sizeof (Size));
    } else {
      Size = Size24;
    }

    if (Size < HeaderSize || Size > Fv.FvLength - Offset || Size > MAX_UINT64 - 7) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    Aligned = (Size + 7) & ~7ULL;
    if (Aligned > Fv.FvLength - Offset) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    if (Ffs.Type == EFI_FV_FILETYPE_FFS_PAD) {
      ++Pad;
    }

    Offset += Aligned;
    ++Count;
    if (Count >= DPX_MAX_FFS_RECORDS) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    Print (L"    Used=N/A Free=N/A Usage=N/A reason=UNCLASSIFIED_FFS_OR_TAIL (%r)\n", Status);
    return Status;
  }

  Free = Fv.FvLength - Offset;
  Percentage = Offset * 10000 / Fv.FvLength;
  Print (L"    Used=%lu Free=%lu Usage=%lu.%02u%% FFS=%u PAD=%u\n",
         Offset, Free, Percentage / 100, (UINT32)(Percentage % 100), Count, Pad);
  Print (L"    Status=VERIFIED_RAW_FVB no file-input needed\n");
  return EFI_SUCCESS;
}

EFI_STATUS
DpXInspectLiveFvbSpaces (
  VOID
  )
{
  EFI_STATUS Status;
  EFI_STATUS ItemStatus;
  EFI_HANDLE *Handles;
  UINTN Count;
  UINTN Index;
  UINTN Verified;
  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *Fvb;

  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol, &gEfiFirmwareVolumeBlockProtocolGuid,
                  NULL, &Count, &Handles
                  );
  if (EFI_ERROR (Status)) {
    Print (L"Live FVB physical inventory unavailable (%r); Total/Used/Free=N/A\n", Status);
    return Status;
  }

  if (Count > DPX_LIVE_FVB_HANDLES) {
    Print (L"Live FVB handles exceeded safe limit\n");
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Print (L"Live read-only FVB space accounting: handles=%u\n", (UINT32)Count);
  Verified = 0;
  for (Index = 0; Index < Count; ++Index) {
    Fvb = NULL;
    ItemStatus = gBS->HandleProtocol (
                        Handles[Index], &gEfiFirmwareVolumeBlockProtocolGuid,
                        (VOID **)&Fvb
                        );
    if (EFI_ERROR (ItemStatus) || Fvb == NULL) {
      Print (L"LiveFVB[%u]: N/A reason=PROTOCOL_HANDLE_FAILURE\n", (UINT32)Index);
      continue;
    }

    ItemStatus = InspectOneFvb (Fvb, Index);
    if (!EFI_ERROR (ItemStatus)) {
      ++Verified;
    }
  }

  FreePool (Handles);
  Print (L"Live FVB verified physical FVs=%u/%u; other FVs remain N/A\n",
         (UINT32)Verified, (UINT32)Count);
  return EFI_SUCCESS;
}
