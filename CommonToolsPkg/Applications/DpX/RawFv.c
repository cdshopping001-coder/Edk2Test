/** @file
  DpX v0.2 read-only FV sizing from a complete, file-backed PI FV at offset 0.
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
#include <Protocol/Shell.h>
#include <Protocol/SimpleFileSystem.h>

#define DPX_MAX_RAW_FV_LENGTH (512ULL * 1024 * 1024)
#define DPX_IO_CHUNK          4096

STATIC EFI_STATUS
ReadAt (EFI_SHELL_PROTOCOL *Shell, SHELL_FILE_HANDLE File, UINT64 Offset, VOID *Data, UINTN Length)
{
  EFI_STATUS Status;
  UINTN ReadSize;
  Status = Shell->SetFilePosition (File, Offset);
  if (EFI_ERROR (Status)) { return Status; }
  ReadSize = Length;
  Status = Shell->ReadFile (File, &ReadSize, Data);
  return EFI_ERROR (Status) ? Status : (ReadSize == Length ? EFI_SUCCESS : EFI_END_OF_FILE);
}

STATIC BOOLEAN
IsErase (CONST UINT8 *Data, UINTN Length, UINT8 Erase)
{
  UINTN I;
  for (I = 0; I < Length; ++I) {
    if (Data[I] != Erase) { return FALSE; }
  }
  return TRUE;
}

STATIC EFI_STATUS
VerifyTail (EFI_SHELL_PROTOCOL *Shell, SHELL_FILE_HANDLE File, UINT64 Begin, UINT64 End, UINT8 Erase)
{
  EFI_STATUS Status;
  UINT8 Buffer[DPX_IO_CHUNK];
  UINTN ReadSize;
  while (Begin < End) {
    ReadSize = (UINTN)((End - Begin) > sizeof(Buffer) ? sizeof(Buffer) : End - Begin);
    Status = ReadAt (Shell, File, Begin, Buffer, ReadSize);
    if (EFI_ERROR (Status)) { return Status; }
    if (!IsErase (Buffer, ReadSize, Erase)) { return EFI_VOLUME_CORRUPTED; }
    Begin += ReadSize;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ValidateFvHeader (CONST UINT8 *Buffer, UINTN Size, UINT64 FvSize)
{
  UINTN At;
  UINT16 Sum;
  UINT64 Blocks;
  UINT64 Add;
  BOOLEAN Terminated;
  EFI_FV_BLOCK_MAP_ENTRY Entry;
  if (Size < OFFSET_OF(EFI_FIRMWARE_VOLUME_HEADER, BlockMap) + 2 * sizeof(Entry) ||
      (Size & 1) != 0) { return EFI_VOLUME_CORRUPTED; }
  Sum = 0;
  for (At = 0; At < Size; At += 2) {
    Sum = (UINT16)(Sum + Buffer[At] + ((UINT16)Buffer[At + 1] << 8));
  }
  if (Sum != 0) { return EFI_CRC_ERROR; }
  Blocks = 0;
  Terminated = FALSE;
  for (At = OFFSET_OF(EFI_FIRMWARE_VOLUME_HEADER, BlockMap);
       At + sizeof(Entry) <= Size; At += sizeof(Entry)) {
    CopyMem (&Entry, Buffer + At, sizeof(Entry));
    if (Entry.NumBlocks == 0 && Entry.Length == 0) { Terminated = TRUE; break; }
    if (Entry.NumBlocks == 0 || Entry.Length == 0) { return EFI_VOLUME_CORRUPTED; }
    Add = (UINT64)Entry.NumBlocks * Entry.Length;
    if (Blocks > FvSize || Add > FvSize - Blocks) { return EFI_VOLUME_CORRUPTED; }
    Blocks += Add;
  }
  return (Terminated && Blocks == FvSize) ? EFI_SUCCESS : EFI_VOLUME_CORRUPTED;
}

STATIC UINT8
FileState (UINT8 RawState, UINT8 Erase)
{
  UINT8 Bit;
  if (Erase == 0xFF) { RawState = (UINT8)~RawState; }
  for (Bit = 0x80; Bit != 0; Bit >>= 1) {
    if ((RawState & Bit) != 0) { return Bit; }
  }
  return 0;
}

STATIC BOOLEAN
ValidFfsChecksum (CONST UINT8 *Raw, UINTN Length)
{
  CONST EFI_FFS_FILE_HEADER *Ffs;
  UINT8 Sum;
  UINTN I;
  Ffs = (CONST EFI_FFS_FILE_HEADER *)Raw;
  Sum = 0;
  for (I = 0; I < Length; ++I) { Sum = (UINT8)(Sum + Raw[I]); }
  Sum = (UINT8)(Sum - Ffs->State - Ffs->IntegrityCheck.Checksum.File);
  return Sum == 0;
}

STATIC EFI_STATUS
ParseRawFv (EFI_SHELL_PROTOCOL *Shell, SHELL_FILE_HANDLE File, UINT64 InputSize)
{
  EFI_STATUS Status;
  EFI_FIRMWARE_VOLUME_HEADER Fv;
  EFI_FIRMWARE_VOLUME_EXT_HEADER Ext;
  EFI_FFS_FILE_HEADER Ffs;
  UINT8 *Header;
  UINT8 FfsRaw[sizeof(EFI_FFS_FILE_HEADER2)];
  UINT8 Erase;
  UINT64 FirstFile;
  UINT64 Offset;
  UINT64 FileBytes;
  UINT64 Aligned;
  UINT64 Free;
  UINT64 Pct100;
  UINT32 Size24;
  UINT32 Count;
  UINT32 Pad;
  UINTN FfsHeaderSize;

  Header = NULL;
  Status = ReadAt (Shell, File, 0, &Fv, sizeof(Fv));
  if (EFI_ERROR(Status)) { Print(L"Total=N/A Used=N/A Free=N/A reason=FV_HEADER_UNREADABLE\n"); return Status; }
  if (Fv.Signature != EFI_FVH_SIGNATURE || Fv.Revision != EFI_FVH_REVISION ||
      Fv.FvLength == 0 || Fv.FvLength > DPX_MAX_RAW_FV_LENGTH ||
      Fv.FvLength > InputSize || Fv.HeaderLength < sizeof(Fv) ||
      Fv.HeaderLength > Fv.FvLength) {
    Print(L"Total=N/A Used=N/A Free=N/A reason=NO_COMPLETE_STANDARD_FV_AT_OFFSET_ZERO\n");
    return EFI_UNSUPPORTED;
  }

  Header = AllocatePool(Fv.HeaderLength);
  if (Header == NULL) { return EFI_OUT_OF_RESOURCES; }
  Status = ReadAt(Shell, File, 0, Header, Fv.HeaderLength);
  if (!EFI_ERROR(Status)) { Status = ValidateFvHeader(Header, Fv.HeaderLength, Fv.FvLength); }
  if (EFI_ERROR(Status)) {
    Print(L"Total=N/A Used=N/A Free=N/A reason=INVALID_FV_HEADER_OR_BLOCKMAP (%r)\n", Status);
    goto End;
  }

  Print(L"FV Total=%lu bytes source=validated PI FV Header\n", Fv.FvLength);
  Erase = (Fv.Attributes & EFI_FVB2_ERASE_POLARITY) ? 0xFF : 0x00;
  FirstFile = Fv.HeaderLength;

  if (Fv.ExtHeaderOffset != 0) {
    if (Fv.ExtHeaderOffset < Fv.HeaderLength ||
        Fv.ExtHeaderOffset > Fv.FvLength ||
        sizeof(Ext) > Fv.FvLength - Fv.ExtHeaderOffset) {
      Status = EFI_VOLUME_CORRUPTED;
      Print(L"  reason=FV_EXT_HEADER_BOUNDS\n");
      goto End;
    }
    Status = ReadAt(Shell, File, Fv.ExtHeaderOffset, &Ext, sizeof(Ext));
    if (EFI_ERROR(Status)) { goto End; }
    if (Ext.ExtHeaderSize < sizeof(Ext) ||
        Ext.ExtHeaderSize > 65536 ||
        Ext.ExtHeaderSize > Fv.FvLength - Fv.ExtHeaderOffset) {
      Status = EFI_VOLUME_CORRUPTED;
      Print(L"  reason=FV_EXT_HEADER_LENGTH\n");
      goto End;
    }
    FirstFile = Fv.ExtHeaderOffset + Ext.ExtHeaderSize;
  }

  if (FirstFile > Fv.FvLength || FirstFile > MAX_UINT64 - 7) {
    Status = EFI_VOLUME_CORRUPTED;
    goto End;
  }
  Offset = (FirstFile + 7) & ~7ULL;
  if (Offset > Fv.FvLength) {
    Status = EFI_VOLUME_CORRUPTED;
    goto End;
  }
  Count = 0;
  Pad = 0;
  while (Offset < Fv.FvLength) {
    if (Fv.FvLength - Offset < sizeof(Ffs)) {
      Status = VerifyTail(Shell, File, Offset, Fv.FvLength, Erase);
      break;
    }
    Status = ReadAt(Shell, File, Offset, &Ffs, sizeof(Ffs));
    if (EFI_ERROR(Status)) { break; }
    if (IsErase((CONST UINT8 *)&Ffs, sizeof(Ffs), Erase)) {
      Status = VerifyTail(Shell, File, Offset, Fv.FvLength, Erase);
      break;
    }
    if (FileState(Ffs.State, Erase) != EFI_FILE_DATA_VALID &&
        FileState(Ffs.State, Erase) != EFI_FILE_MARKED_FOR_UPDATE &&
        FileState(Ffs.State, Erase) != EFI_FILE_DELETED) {
      Status = EFI_UNSUPPORTED;
      Print(L"  reason=FFS_STATE_UNSUPPORTED offset=0x%lx\n", Offset);
      break;
    }
    FfsHeaderSize = (Ffs.Attributes & FFS_ATTRIB_LARGE_FILE) ?
                    sizeof(EFI_FFS_FILE_HEADER2) : sizeof(EFI_FFS_FILE_HEADER);
    if (FfsHeaderSize > Fv.FvLength - Offset) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }
    Status = ReadAt(Shell, File, Offset, FfsRaw, FfsHeaderSize);
    if (EFI_ERROR(Status)) { break; }
    if (!ValidFfsChecksum(FfsRaw, FfsHeaderSize)) {
      Status = EFI_CRC_ERROR;
      Print(L"  reason=FFS_HEADER_CHECKSUM offset=0x%lx\n", Offset);
      break;
    }
    Size24 = (UINT32)Ffs.Size[0] | ((UINT32)Ffs.Size[1] << 8) |
             ((UINT32)Ffs.Size[2] << 16);
    if (Ffs.Attributes & FFS_ATTRIB_LARGE_FILE) {
      if (Size24 != 0) { Status = EFI_VOLUME_CORRUPTED; break; }
      CopyMem(&FileBytes, FfsRaw + OFFSET_OF(EFI_FFS_FILE_HEADER2, ExtendedSize),
              sizeof(FileBytes));
    } else {
      FileBytes = Size24;
    }
    if (FileBytes < FfsHeaderSize || FileBytes > Fv.FvLength - Offset ||
        FileBytes > MAX_UINT64 - 7) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }
    Aligned = (FileBytes + 7) & ~7ULL;
    if (Aligned > Fv.FvLength - Offset) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }
    if (Ffs.Type == EFI_FV_FILETYPE_FFS_PAD) { ++Pad; }
    Offset += Aligned;
    ++Count;
    if (Count >= DPX_MAX_FFS_RECORDS) {
      Status = EFI_OUT_OF_RESOURCES;
      Print(L"  reason=FFS_LIMIT_REACHED\n");
      break;
    }
  }
  if (!EFI_ERROR(Status)) {
    Free = Fv.FvLength - Offset;
    Pct100 = (Offset * 10000) / Fv.FvLength;
    Print(L"  Total=%lu Used=%lu Free=%lu Usage=%lu.%02u%%\n",
          Fv.FvLength, Offset, Free, Pct100 / 100, (UINT32)(Pct100 % 100));
    Print(L"  FFS=%u PAD=%u ErasePolarity=%u status=verified_contiguous_layout\n",
          Count, Pad, Erase == 0xFF ? 1 : 0);
  }
End:
  if (EFI_ERROR(Status)) {
    Print(L"  Used=N/A Free=N/A Usage=N/A reason=UNVALIDATED_FFS_OR_ERASED_TAIL (%r)\n", Status);
  }
  FreePool(Header);
  return Status;
}

EFI_STATUS
DpXAnalyzeRawFirmwareFile (IN CONST CHAR16 *Path)
{
  EFI_SHELL_PROTOCOL *Shell;
  SHELL_FILE_HANDLE File;
  EFI_STATUS Status;
  UINT64 Size;

  if (Path == NULL || Path[0] == L'\0') { return EFI_INVALID_PARAMETER; }
  Shell = NULL;
  File = NULL;
  Status = gBS->LocateProtocol(&gEfiShellProtocolGuid, NULL, (VOID **)&Shell);
  if (EFI_ERROR(Status) || Shell == NULL) {
    Print(L"Shell Protocol unavailable: %r\n", Status);
    return EFI_UNSUPPORTED;
  }
  Status = Shell->OpenFileByName(Path, &File, EFI_FILE_MODE_READ);
  if (EFI_ERROR(Status)) { Print(L"Cannot read input %s: %r\n", Path, Status); return Status; }
  Size = 0;
  Status = Shell->GetFileSize(File, &Size);
  if (!EFI_ERROR(Status) && Size > DPX_MAX_RAW_FV_LENGTH) {
    Status = EFI_UNSUPPORTED;
    Print(L"Input too large (limit 512 MiB); N/A\n");
  }
  if (!EFI_ERROR(Status)) {
    Print(L"Input=%s length=%lu bytes (single standard FV at offset 0)\n", Path, Size);
    Status = ParseRawFv(Shell, File, Size);
  }
  Shell->CloseFile(File);
  return Status;
}
