/** @file
  Parse an ACPI FPDT table supplied as a Shell file. Pointer values in ACPI
  records are untrusted physical addresses and are NOT dereferenced here.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <IndustryStandard/Acpi50.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Shell.h>
#include <Protocol/SimpleFileSystem.h>

#define DPX_FPDT_ACPI_LIMIT  (1024U * 1024U)
#define DPX_FPDT_IO_CHUNK    4096

STATIC EFI_STATUS
ReadFpdtAt (
  IN EFI_SHELL_PROTOCOL  *Shell,
  IN SHELL_FILE_HANDLE   File,
  IN UINT64              At,
  OUT VOID               *Buffer,
  IN UINTN               Size
  )
{
  EFI_STATUS  Status;
  UINTN       Actual;

  Status = Shell->SetFilePosition (File, At);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Actual = Size;
  Status = Shell->ReadFile (File, &Actual, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return (Actual == Size) ? EFI_SUCCESS : EFI_END_OF_FILE;
}

STATIC EFI_STATUS
AnalyzeFpdtAcpiFile (
  IN EFI_SHELL_PROTOCOL  *Shell,
  IN SHELL_FILE_HANDLE   File,
  IN UINT64              FileSize
  )
{
  EFI_STATUS                                          Status;
  EFI_ACPI_DESCRIPTION_HEADER                         Acpi;
  EFI_ACPI_5_0_FPDT_PERFORMANCE_RECORD_HEADER         Record;
  EFI_ACPI_5_0_FPDT_BOOT_PERFORMANCE_TABLE_POINTER_RECORD BootPtr;
  EFI_ACPI_5_0_FPDT_S3_PERFORMANCE_TABLE_POINTER_RECORD   S3Ptr;
  UINT8                                               Chunk[DPX_FPDT_IO_CHUNK];
  UINT8                                               Sum;
  UINT64                                              Offset;
  UINT64                                              Position;
  UINTN                                               Part;
  UINTN                                               I;
  UINT32                                              Records;
  UINT32                                              BootRecords;
  UINT32                                              S3Records;
  UINT32                                              OtherRecords;

  if ((FileSize < sizeof (Acpi)) || (FileSize > DPX_FPDT_ACPI_LIMIT)) {
    Print (L"FPDT status=invalid reason=FILE_LENGTH_OUT_OF_RANGE\n");
    return EFI_BAD_BUFFER_SIZE;
  }

  Status = ReadFpdtAt (Shell, File, 0, &Acpi, sizeof (Acpi));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Acpi.Signature != EFI_ACPI_5_0_FIRMWARE_PERFORMANCE_DATA_TABLE_SIGNATURE) {
    Print (L"FPDT status=invalid reason=BAD_SIGNATURE\n");
    return EFI_UNSUPPORTED;
  }

  if ((Acpi.Length < sizeof (Acpi)) ||
      (Acpi.Length > FileSize) ||
      (Acpi.Length > DPX_FPDT_ACPI_LIMIT))
  {
    Print (L"FPDT status=invalid reason=BAD_TABLE_LENGTH\n");
    return EFI_VOLUME_CORRUPTED;
  }

  // ACPI table checksum must cover every byte (including unknown records).
  Sum = 0;
  for (Position = 0; Position < Acpi.Length; Position += Part) {
    Part = (UINTN)(((UINT64)Acpi.Length - Position > sizeof (Chunk)) ?
                   sizeof (Chunk) : ((UINT64)Acpi.Length - Position));
    Status = ReadFpdtAt (Shell, File, Position, Chunk, Part);
    if (EFI_ERROR (Status)) {
      Print (L"FPDT status=invalid reason=TABLE_READ_FAILED\n");
      return Status;
    }

    for (I = 0; I < Part; ++I) {
      Sum = (UINT8)(Sum + Chunk[I]);
    }
  }

  if (Sum != 0) {
    Print (L"FPDT status=invalid reason=ACPI_CHECKSUM_INVALID\n");
    return EFI_CRC_ERROR;
  }

  Print (L"FPDT signature=FPDT size=%u revision=%u source=Shell file checksum=valid\n",
         Acpi.Length, (UINT32)Acpi.Revision);
  if (FileSize > Acpi.Length) {
    Print (L"  Warning=TRAILING_BYTES_IGNORED\n");
  }

  Offset = sizeof (Acpi);
  Records = 0;
  BootRecords = 0;
  S3Records = 0;
  OtherRecords = 0;
  while (Offset < Acpi.Length) {
    if ((UINT64)Acpi.Length - Offset < sizeof (Record)) {
      Print (L"FPDT status=invalid reason=TRUNCATED_RECORD_HEADER\n");
      return EFI_VOLUME_CORRUPTED;
    }

    Status = ReadFpdtAt (Shell, File, Offset, &Record, sizeof (Record));
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((Record.Length < sizeof (Record)) ||
        (Record.Length > (UINT64)Acpi.Length - Offset))
    {
      Print (L"FPDT status=invalid reason=RECORD_LENGTH_INVALID at=0x%lx\n", Offset);
      return EFI_VOLUME_CORRUPTED;
    }

    if (Records >= 4096) {
      Print (L"FPDT status=invalid reason=RECORD_LIMIT\n");
      return EFI_OUT_OF_RESOURCES;
    }

    if (Record.Type == EFI_ACPI_5_0_FPDT_RECORD_TYPE_FIRMWARE_BASIC_BOOT_POINTER) {
      if ((Record.Length < sizeof (BootPtr)) ||
          (Record.Revision != EFI_ACPI_5_0_FPDT_RECORD_REVISION_FIRMWARE_BASIC_BOOT_POINTER) ||
          (BootRecords != 0))
      {
        Print (L"FPDT status=invalid reason=BOOT_POINTER_FORMAT_OR_DUPLICATE\n");
        return EFI_VOLUME_CORRUPTED;
      }

      Status = ReadFpdtAt (Shell, File, Offset, &BootPtr, sizeof (BootPtr));
      if (EFI_ERROR (Status)) {
        return Status;
      }

      Print (L"  BootPerformanceTablePointer=0x%lx (untrusted physical address; not dereferenced)\n",
             BootPtr.BootPerformanceTablePointer);
      ++BootRecords;
    } else if (Record.Type == EFI_ACPI_5_0_FPDT_RECORD_TYPE_S3_PERFORMANCE_TABLE_POINTER) {
      if ((Record.Length < sizeof (S3Ptr)) ||
          (Record.Revision != EFI_ACPI_5_0_FPDT_RECORD_REVISION_S3_PERFORMANCE_TABLE_POINTER) ||
          (S3Records != 0))
      {
        Print (L"FPDT status=invalid reason=S3_POINTER_FORMAT_OR_DUPLICATE\n");
        return EFI_VOLUME_CORRUPTED;
      }

      Status = ReadFpdtAt (Shell, File, Offset, &S3Ptr, sizeof (S3Ptr));
      if (EFI_ERROR (Status)) {
        return Status;
      }

      Print (L"  S3PerformanceTablePointer=0x%lx (not dereferenced)\n",
             S3Ptr.S3PerformanceTablePointer);
      ++S3Records;
    } else {
      ++OtherRecords;
    }

    ++Records;
    Offset += Record.Length;
  }

  Print (L"FPDT status=verified Records=%u BootPointers=%u S3Pointers=%u OtherRecords=%u\n",
         Records, BootRecords, S3Records, OtherRecords);
  if (BootRecords == 0) {
    Print (L"FPDT Warning=BOOT_POINTER_NOT_PRESENT\n");
  }

  return EFI_SUCCESS;
}

EFI_STATUS
DpXParseFpdtAcpiFile (
  IN CONST CHAR16  *Path
  )
{
  EFI_STATUS          Status;
  EFI_SHELL_PROTOCOL  *Shell;
  SHELL_FILE_HANDLE   File;
  UINT64              Size;

  if ((Path == NULL) || (Path[0] == L'\0')) {
    return EFI_INVALID_PARAMETER;
  }

  Shell = NULL;
  File = NULL;
  Status = gBS->LocateProtocol (&gEfiShellProtocolGuid, NULL, (VOID **)&Shell);
  if (EFI_ERROR (Status) || (Shell == NULL)) {
    Print (L"FPDT Shell Protocol unavailable (%r)\n", Status);
    return EFI_UNSUPPORTED;
  }

  Status = Shell->OpenFileByName (Path, &File, EFI_FILE_MODE_READ);
  if (EFI_ERROR (Status)) {
    Print (L"FPDT file unavailable: %r\n", Status);
    return Status;
  }

  Size = 0;
  Status = Shell->GetFileSize (File, &Size);
  if (!EFI_ERROR (Status)) {
    Status = AnalyzeFpdtAcpiFile (Shell, File, Size);
  }

  Shell->CloseFile (File);
  return Status;
}
