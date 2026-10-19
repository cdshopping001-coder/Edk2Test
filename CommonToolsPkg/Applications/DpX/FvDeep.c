/** @file
  Recursive, read-only PI section analyzer for DpX.
  Standard EFI compression is decoded in memory with bounded buffers.
  FV_IMAGE payloads are recursively walked for nested FFS, without
  double-counting their bytes in the parent's physical FV Used/Free.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"
#include <Guid/FirmwareFileSystem2.h>
#include <Guid/FirmwareFileSystem3.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiDecompressLib.h>
#include <Library/UefiLib.h>
#include <Pi/PiFirmwareVolume.h>
#include <Pi/PiFirmwareFile.h>

#define DPX_DEEP_MAX_DEPTH    4
#define DPX_DEEP_INPUT_LIMIT  (4U * 1024U * 1024U)
#define DPX_DEEP_OUTPUT_LIMIT (8U * 1024U * 1024U)
#define DPX_DEEP_SCRATCH_MAX  (8U * 1024U * 1024U)
#define DPX_DEEP_MAX_FILES    8192
#define DPX_DEEP_MAX_SECTIONS 32768

STATIC
UINT8
EffectiveState (
  UINT8 State,
  UINT8 Erase
  )
{
  UINT8 Flag;
  if (Erase == 0xFF) {
    State = (UINT8)~State;
  }

  for (Flag = 0x80; Flag != 0; Flag >>= 1) {
    if ((Flag & State) != 0) {
      return Flag;
    }
  }

  return 0;
}

STATIC
BOOLEAN
FfsHeaderOkay (
  IN CONST UINT8 *Bytes,
  IN UINTN Size
  )
{
  EFI_FFS_FILE_HEADER H;
  UINT8 Sum;
  UINTN I;
  if (Size < sizeof (H)) {
    return FALSE;
  }

  CopyMem (&H, Bytes, sizeof (H));
  Sum = 0;
  for (I = 0; I < Size; ++I) {
    Sum = (UINT8)(Sum + Bytes[I]);
  }

  return (UINT8)(Sum - H.State - H.IntegrityCheck.Checksum.File) == 0;
}

STATIC
BOOLEAN
FvHeaderOkay (
  IN CONST UINT8 *Bytes,
  IN UINTN Size,
  OUT EFI_FIRMWARE_VOLUME_HEADER *Fv
  )
{
  UINT16 Checksum;
  UINTN At;
  UINT64 UsedBlocks;
  UINT64 Added;
  BOOLEAN End;
  EFI_FV_BLOCK_MAP_ENTRY Entry;
  if (Size < sizeof (*Fv)) {
    return FALSE;
  }

  CopyMem (Fv, Bytes, sizeof (*Fv));
  if (Fv->Signature != EFI_FVH_SIGNATURE ||
      Fv->Revision != EFI_FVH_REVISION ||
      Fv->FvLength < sizeof (*Fv) ||
      Fv->FvLength > Size ||
      Fv->HeaderLength < sizeof (*Fv) ||
      Fv->HeaderLength > Fv->FvLength ||
      (Fv->HeaderLength & 1) != 0 ||
      (!CompareGuid (&Fv->FileSystemGuid, &gEfiFirmwareFileSystem2Guid) &&
       !CompareGuid (&Fv->FileSystemGuid, &gEfiFirmwareFileSystem3Guid)))
  {
    return FALSE;
  }

  Checksum = 0;
  for (At = 0; At < Fv->HeaderLength; At += 2) {
    Checksum = (UINT16)(Checksum + Bytes[At] +
                       ((UINT16)Bytes[At + 1] << 8));
  }

  if (Checksum != 0) {
    return FALSE;
  }

  UsedBlocks = 0;
  End = FALSE;
  for (At = OFFSET_OF (EFI_FIRMWARE_VOLUME_HEADER, BlockMap);
       At + sizeof (Entry) <= Fv->HeaderLength;
       At += sizeof (Entry))
  {
    CopyMem (&Entry, Bytes + At, sizeof (Entry));
    if (Entry.NumBlocks == 0 && Entry.Length == 0) {
      End = TRUE;
      break;
    }

    if (Entry.NumBlocks == 0 || Entry.Length == 0) {
      return FALSE;
    }

    Added = (UINT64)Entry.NumBlocks * Entry.Length;
    if (Added > Fv->FvLength - UsedBlocks) {
      return FALSE;
    }

    UsedBlocks += Added;
  }

  return End && UsedBlocks == Fv->FvLength;
}

STATIC
BOOLEAN
IsErased (
  IN CONST UINT8 *Bytes,
  IN UINTN Size,
  IN UINT8 Erase
  )
{
  UINTN I;
  for (I = 0; I < Size; ++I) {
    if (Bytes[I] != Erase) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC
CONST CHAR16 *
DpXSectionName (
  IN UINT8 Kind
  )
{
  switch (Kind) {
    case EFI_SECTION_PE32:                    return L"PE32";
    case EFI_SECTION_PIC:                     return L"PIC";
    case EFI_SECTION_TE:                      return L"TE";
    case EFI_SECTION_DXE_DEPEX:               return L"DXE_DEPEX";
    case EFI_SECTION_PEI_DEPEX:               return L"PEI_DEPEX";
    case EFI_SECTION_USER_INTERFACE:          return L"UI";
    case EFI_SECTION_VERSION:                 return L"VERSION";
    case EFI_SECTION_RAW:                     return L"RAW";
    case EFI_SECTION_COMPRESSION:             return L"COMPRESSION";
    case EFI_SECTION_GUID_DEFINED:            return L"GUID_DEFINED";
    case EFI_SECTION_FIRMWARE_VOLUME_IMAGE:   return L"FV_IMAGE";
    case EFI_SECTION_DISPOSABLE:              return L"DISPOSABLE";
    default:                                  return L"OTHER";
  }
}

/**
  Decode the UI section as a bounded UTF-16 label within its actual FFS.
  An oversized/unterminated section remains explicitly partial; never treat
  a free-text UI name as a verified source-level function symbol.
**/
STATIC
VOID
DpXPrintUiSection (
  IN CONST UINT8 *Raw,
  IN UINTN Bytes,
  IN UINTN TreeLevel
  )
{
  CHAR16 Text[129];
  CHAR16 Value;
  UINTN Index;
  BOOLEAN Terminated;

  ZeroMem (Text, sizeof (Text));
  Terminated = FALSE;
  if ((Raw == NULL) || ((Bytes & 1) != 0) || Bytes == 0) {
    DpXTreePrefix (TreeLevel, TRUE);
    Print (L"UI Name=N/A reason=INVALID_UTF16_SECTION\n");
    return;
  }

  for (Index = 0; Index < 128 && Index * 2 + 1 < Bytes; ++Index) {
    CopyMem (&Value, Raw + Index * 2, sizeof (Value));
    if (Value == 0) {
      Terminated = TRUE;
      break;
    }

    Text[Index] = (Value < 0x20 || Value == 0x7F) ? L'?' : Value;
  }

  DpXTreePrefix (TreeLevel, TRUE);
  if (Index == 0 || !Terminated) {
    Print (L"UI Name=N/A reason=EMPTY_OR_UNTERMINATED_UI_SECTION\n");
    return;
  }

  Print (L"UI Name=%s source=FFS_UI_SECTION\n", Text);
}

STATIC
EFI_STATUS
DpXInspectSectionsInternal (
  IN CONST UINT8 *Bytes,
  IN UINTN Size,
  IN UINTN Depth,
  IN BOOLEAN PhysicalOffset,
  IN UINT64 OffsetBase,
  IN UINTN TreeLevel
  );

STATIC
EFI_STATUS
WalkNestedVolume (
  IN CONST UINT8 *Bytes,
  IN UINTN Size,
  IN UINTN Depth,
  IN UINTN TreeLevel
  )
{
  EFI_FIRMWARE_VOLUME_HEADER Fv;
  EFI_FIRMWARE_VOLUME_EXT_HEADER Ext;
  EFI_FFS_FILE_HEADER Ffs;
  UINT64 Offset;
  UINT64 FileBytes;
  UINT64 Aligned;
  UINTN HdrBytes;
  UINT32 Size24;
  UINTN Count;
  UINTN Pad;
  UINTN Deleted;
  UINT8 Erase;
  UINT8 State;
  UINTN FirstFile;
  EFI_STATUS Status;

  if (Depth >= DPX_DEEP_MAX_DEPTH || !FvHeaderOkay (Bytes, Size, &Fv)) {
    DpXTreePrefix (TreeLevel, TRUE);
    Print (L"NestedFV=N/A reason=DEPTH_OR_INVALID_FV_HEADER\n");
    return EFI_UNSUPPORTED;
  }

  Erase = (Fv.Attributes & EFI_FVB2_ERASE_POLARITY) != 0 ? 0xFF : 0;
  FirstFile = Fv.HeaderLength;
  if (Fv.ExtHeaderOffset != 0) {
    if (Fv.ExtHeaderOffset < Fv.HeaderLength ||
        Fv.ExtHeaderOffset > Fv.FvLength ||
        sizeof (Ext) > Fv.FvLength - Fv.ExtHeaderOffset)
    {
      return EFI_VOLUME_CORRUPTED;
    }

    CopyMem (&Ext, Bytes + Fv.ExtHeaderOffset, sizeof (Ext));
    if (Ext.ExtHeaderSize < sizeof (Ext) ||
        Ext.ExtHeaderSize > Fv.FvLength - Fv.ExtHeaderOffset)
    {
      return EFI_VOLUME_CORRUPTED;
    }

    FirstFile = Fv.ExtHeaderOffset + Ext.ExtHeaderSize;
  }

  Offset = (FirstFile + 7ULL) & ~7ULL;
  if (Offset > Fv.FvLength) {
    return EFI_VOLUME_CORRUPTED;
  }

  Count = 0;
  Pad = 0;
  Deleted = 0;
  Status = EFI_SUCCESS;
  DpXTreePrefix (TreeLevel, FALSE);
  Print (L"NestedFV depth=%u NameGuid=", (UINT32)Depth);
  if (Fv.ExtHeaderOffset != 0) {
    Print (L"%g", &Ext.FvName);
  } else {
    Print (L"N/A");
  }

  Print (L" FormatGuid=%g Total=%lu bytes (inside parent payload)\n",
         &Fv.FileSystemGuid, Fv.FvLength);

  while (Offset < Fv.FvLength) {
    if (Fv.FvLength - Offset < sizeof (Ffs)) {
      if (!IsErased (Bytes + (UINTN)Offset,
                     (UINTN)(Fv.FvLength - Offset), Erase))
      {
        Status = EFI_VOLUME_CORRUPTED;
      }

      break;
    }

    CopyMem (&Ffs, Bytes + (UINTN)Offset, sizeof (Ffs));
    if (IsErased ((CONST UINT8 *)&Ffs, sizeof (Ffs), Erase)) {
      if (!IsErased (Bytes + (UINTN)Offset,
                     (UINTN)(Fv.FvLength - Offset), Erase))
      {
        Status = EFI_VOLUME_CORRUPTED;
      }

      break;
    }

    State = EffectiveState (Ffs.State, Erase);
    if (State != EFI_FILE_DATA_VALID &&
        State != EFI_FILE_MARKED_FOR_UPDATE &&
        State != EFI_FILE_DELETED)
    {
      Status = EFI_UNSUPPORTED;
      break;
    }

    HdrBytes = (Ffs.Attributes & FFS_ATTRIB_LARGE_FILE) != 0 ?
               sizeof (EFI_FFS_FILE_HEADER2) : sizeof (EFI_FFS_FILE_HEADER);
    if (HdrBytes > Fv.FvLength - Offset ||
        !FfsHeaderOkay (Bytes + (UINTN)Offset, HdrBytes))
    {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    Size24 = (UINT32)Ffs.Size[0] |
             ((UINT32)Ffs.Size[1] << 8) |
             ((UINT32)Ffs.Size[2] << 16);
    if ((Ffs.Attributes & FFS_ATTRIB_LARGE_FILE) != 0) {
      if (Size24 != 0) {
        Status = EFI_VOLUME_CORRUPTED;
        break;
      }

      CopyMem (&FileBytes,
               Bytes + (UINTN)Offset +
               OFFSET_OF (EFI_FFS_FILE_HEADER2, ExtendedSize),
               sizeof (FileBytes));
    } else {
      FileBytes = Size24;
    }

    if (FileBytes < HdrBytes || FileBytes > Fv.FvLength - Offset ||
        FileBytes > MAX_UINT64 - 7)
    {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    Aligned = (FileBytes + 7) & ~7ULL;
    if (Aligned > Fv.FvLength - Offset) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    ++Count;
    if (Ffs.Type == EFI_FV_FILETYPE_FFS_PAD) {
      ++Pad;
    }

    if (State == EFI_FILE_DELETED) {
      ++Deleted;
    }

    DpXTreePrefix (TreeLevel + 1, FALSE);
    Print (L"NestedFFS[%u] GUID=%g Type=0x%02x Offset=0x%lx PhysicalSize=%lu State=%s\n",
           (UINT32)(Count - 1), &Ffs.Name, Ffs.Type,
           Offset, FileBytes,
           State == EFI_FILE_DELETED ? L"DELETED_UNRECLAIMED" : L"allocated");
    if ((Ffs.Type != EFI_FV_FILETYPE_FFS_PAD) &&
        (State != EFI_FILE_DELETED) &&
        (FileBytes > HdrBytes))
    {
      DpXInspectSectionsInternal (
        Bytes + (UINTN)Offset + HdrBytes,
        (UINTN)(FileBytes - HdrBytes),
        Depth + 1, FALSE, 0, TreeLevel + 2
        );
    }

    Offset += Aligned;
    if (Count >= DPX_DEEP_MAX_FILES) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    DpXTreePrefix (TreeLevel + 1, TRUE);
    Print (L"NestedFV FFSCount=N/A reason=INVALID_OR_UNKNOWN_LAYOUT (%r)\n",
           Status);
  } else {
    DpXTreePrefix (TreeLevel + 1, TRUE);
    Print (L"NestedFV FFSCount=%u PAD=%u DeletedUnreclaimed=%u Used=%lu Free=%lu\n",
           (UINT32)Count, (UINT32)Pad, (UINT32)Deleted,
           Offset, Fv.FvLength - Offset);
  }

  DpXTreePrefix (TreeLevel + 1, TRUE);
  Print (L"Accounting=CHILD_ONLY_DO_NOT_ADD_TO_PARENT\n");
  return Status;
}

STATIC
EFI_STATUS
DpXInspectSectionsInternal (
  IN CONST UINT8 *Bytes,
  IN UINTN Size,
  IN UINTN Depth,
  IN BOOLEAN PhysicalOffset,
  IN UINT64 OffsetBase,
  IN UINTN TreeLevel
  )
{
  EFI_STATUS Status;
  RETURN_STATUS LibStatus;
  UINTN At;
  UINTN HdrBytes;
  UINTN SectionSize;
  UINTN PayloadStart;
  UINT32 ShortSize;
  UINT32 Expected;
  UINT32 OutputBytes;
  UINT32 ScratchBytes;
  UINT32 CompressedBytes;
  UINT8 Kind;
  UINT8 *Decoded;
  VOID *Scratch;
  UINTN Count;
  EFI_GUID Guided;
  UINT16 DataOffset;
  UINT16 Flags;

  if (Bytes == NULL || Depth >= DPX_DEEP_MAX_DEPTH) {
    return EFI_UNSUPPORTED;
  }

  if (Size > DPX_DEEP_OUTPUT_LIMIT) {
    DpXTreePrefix (TreeLevel, TRUE);
    Print (L"SectionDetail=N/A reason=SECTION_BYTES_OVER_LIMIT\n");
    return EFI_UNSUPPORTED;
  }

  At = 0;
  Count = 0;
  Status = EFI_SUCCESS;
  while (At < Size) {
    // 0-3 bytes of padding after the final section are acceptable.
    if (Size - At < sizeof (EFI_COMMON_SECTION_HEADER)) {
      break;
    }

    ShortSize = (UINT32)Bytes[At] |
                ((UINT32)Bytes[At + 1] << 8) |
                ((UINT32)Bytes[At + 2] << 16);
    Kind = Bytes[At + 3];
    HdrBytes = sizeof (EFI_COMMON_SECTION_HEADER);
    if (ShortSize == 0xFFFFFF) {
      HdrBytes = sizeof (EFI_COMMON_SECTION_HEADER2);
      if (Size - At < HdrBytes) {
        Status = EFI_VOLUME_CORRUPTED;
        break;
      }

      CopyMem (&Expected, Bytes + At + 4, sizeof (Expected));
      SectionSize = (UINTN)Expected;
    } else {
      SectionSize = ShortSize;
    }

    if (SectionSize < HdrBytes || SectionSize > Size - At) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    PayloadStart = At + HdrBytes;
    DpXTreePrefix (TreeLevel, At + ((SectionSize + 3) & ~(UINTN)3) >= Size);
    Print (L"Section[%s] depth=%u Type=0x%02x RawBytes=%u Offset=",
           DpXSectionName (Kind), (UINT32)Depth, Kind, (UINT32)SectionSize);
    if (PhysicalOffset) {
      Print (L"0x%lx (FV-relative)\n", OffsetBase + At);
    } else {
      Print (L"N/A (decoded/logical bytes)\n");
    }

    if (Kind == EFI_SECTION_COMPRESSION) {
      if (SectionSize < HdrBytes + sizeof (UINT32) + 1) {
        Status = EFI_VOLUME_CORRUPTED;
        break;
      }

      CopyMem (&Expected, Bytes + PayloadStart, sizeof (Expected));
      PayloadStart += sizeof (Expected);
      CompressedBytes = (UINT32)(SectionSize - (PayloadStart - At) - 1);
      Kind = Bytes[PayloadStart++];
      DpXTreePrefix (TreeLevel + 1, FALSE);
      Print (L"Compression Type=%u Stored=%u DeclaredDecoded=%u\n",
             Kind, CompressedBytes, Expected);
      if (Expected > DPX_DEEP_OUTPUT_LIMIT ||
          (CompressedBytes == 0 && Expected != 0) ||
          (CompressedBytes != 0 && Expected > (UINT64)CompressedBytes * 128))
      {
        Print (L"      Decompress=N/A reason=SIZE_OR_EXPANSION_LIMIT\n");
      } else if (Kind == EFI_NOT_COMPRESSED) {
        if (CompressedBytes == Expected) {
          DpXTreePrefix (TreeLevel + 1, FALSE);
          Print (L"Decompress=NONE decodedBytes=%u\n", Expected);
          DpXInspectSectionsInternal (
            Bytes + PayloadStart, Expected,
            Depth + 1, FALSE, 0, TreeLevel + 2
            );
        } else {
          Print (L"      Decompress=N/A reason=RAW_LENGTH_MISMATCH\n");
        }
      } else if (Kind == EFI_STANDARD_COMPRESSION) {
        OutputBytes = 0;
        ScratchBytes = 0;
        LibStatus = UefiDecompressGetInfo (
                      Bytes + PayloadStart, CompressedBytes,
                      &OutputBytes, &ScratchBytes
                      );
        if (RETURN_ERROR (LibStatus) ||
            OutputBytes != Expected ||
            OutputBytes > DPX_DEEP_OUTPUT_LIMIT ||
            ScratchBytes > DPX_DEEP_SCRATCH_MAX)
        {
          Print (L"      Decompress=N/A reason=UNSUPPORTED_OR_UNTRUSTED_STREAM\n");
        } else {
          Decoded = AllocatePool (OutputBytes == 0 ? 1 : OutputBytes);
          Scratch = AllocatePool (ScratchBytes == 0 ? 1 : ScratchBytes);
          if ((Decoded == NULL) || (Scratch == NULL)) {
            Print (L"      Decompress=N/A reason=OUT_OF_RESOURCES\n");
          } else {
            LibStatus = UefiDecompress (
                          Bytes + PayloadStart, Decoded, Scratch
                          );
            if (RETURN_ERROR (LibStatus)) {
              Print (L"      Decompress=N/A reason=DECODE_FAILED\n");
            } else {
              DpXTreePrefix (TreeLevel + 1, FALSE);
              Print (L"Decompress=UEFI_STANDARD decodedBytes=%u; parent physical usage UNCHANGED\n",
                     OutputBytes);
              DpXInspectSectionsInternal (
                Decoded, OutputBytes, Depth + 1, FALSE, 0, TreeLevel + 2
                );
            }
          }

          if (Scratch != NULL) {
            FreePool (Scratch);
          }

          if (Decoded != NULL) {
            FreePool (Decoded);
          }
        }
      } else {
        Print (L"      Decompress=UNSUPPORTED_CODEC; not counted as a new FFS\n");
      }
    } else if (Kind == EFI_SECTION_USER_INTERFACE) {
      DpXPrintUiSection (
        Bytes + PayloadStart, SectionSize - HdrBytes, TreeLevel + 1
        );
    } else if (Kind == EFI_SECTION_FIRMWARE_VOLUME_IMAGE) {
      DpXTreePrefix (TreeLevel + 1, FALSE);
      Print (L"NestedFV candidate size=%u\n",
             (UINT32)(SectionSize - HdrBytes));
      WalkNestedVolume (
        Bytes + PayloadStart, SectionSize - HdrBytes, Depth + 1,
        TreeLevel + 2
        );
    } else if (Kind == EFI_SECTION_GUID_DEFINED) {
      if (SectionSize < HdrBytes + sizeof (Guided) + sizeof (UINT16) * 2) {
        Status = EFI_VOLUME_CORRUPTED;
        break;
      }

      CopyMem (&Guided, Bytes + PayloadStart, sizeof (Guided));
      CopyMem (&DataOffset, Bytes + PayloadStart + sizeof (Guided),
               sizeof (DataOffset));
      CopyMem (&Flags,
               Bytes + PayloadStart + sizeof (Guided) + sizeof (DataOffset),
               sizeof (Flags));
      DpXTreePrefix (TreeLevel + 1, FALSE);
      Print (L"Guided Guid=%g Attr=0x%04x\n", &Guided, Flags);
      if (DataOffset < HdrBytes + sizeof (Guided) + 4 ||
          DataOffset > SectionSize)
      {
        Status = EFI_VOLUME_CORRUPTED;
        break;
      }

      if ((Flags & EFI_GUIDED_SECTION_PROCESSING_REQUIRED) != 0) {
        Print (L"      GuidedDecode=N/A reason=VENDOR_OR_GUID_CODEC_REQUIRED\n");
      } else {
        DpXInspectSectionsInternal (
          Bytes + At + DataOffset,
          SectionSize - DataOffset,
          Depth + 1, FALSE, 0, TreeLevel + 2
          );
      }
    } else if (Kind == EFI_SECTION_DISPOSABLE) {
      DpXInspectSectionsInternal (
        Bytes + PayloadStart, SectionSize - HdrBytes,
        Depth + 1, FALSE, 0, TreeLevel + 1
        );
    }

    ++Count;
    if (Count >= DPX_DEEP_MAX_SECTIONS) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }

    if (SectionSize > MAX_UINTN - 3) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    At += (SectionSize + 3) & ~(UINTN)3;
    if (At > Size && At - Size > 3) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    DpXTreePrefix (TreeLevel, TRUE);
    Print (L"SectionDetail=N/A reason=MALFORMED_SECTION_LAYOUT (%r)\n", Status);
  }

  return Status;
}

EFI_STATUS
DpXInspectFfsSections (
  IN CONST UINT8 *Bytes,
  IN UINTN Size,
  IN BOOLEAN OffsetValid,
  IN UINT64 FvRelativeOffset
  )
{
  if (Size > DPX_DEEP_INPUT_LIMIT) {
    Print (L"    SectionDetail=N/A reason=FFS_PAYLOAD_TOO_LARGE\n");
    return EFI_UNSUPPORTED;
  }

  return DpXInspectSectionsInternal (
           Bytes, Size, 0, OffsetValid, FvRelativeOffset, 2
           );
}
