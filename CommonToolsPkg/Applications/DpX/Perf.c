/** @file
  DpX v0.4 — bounded, read-only ACPI FPDT Basic Boot Performance Table parser.

  This first stage accepts a raw FBPT file supplied in UEFI Shell. It does not
  dereference any unvalidated physical ACPI pointer, and it does not claim
  OS-loader timestamps when an EFI Shell boot has not reached the OS loader.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <IndustryStandard/Acpi50.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Shell.h>
#include <Protocol/SimpleFileSystem.h>

#define DPX_MAX_FBPT_FILE_BYTES  (64ULL * 1024 * 1024)
#define DPX_MAX_FBPT_RECORDS     131072

STATIC
EFI_STATUS
FpdtReadExact (
  IN EFI_SHELL_PROTOCOL  *Shell,
  IN SHELL_FILE_HANDLE   File,
  IN UINT64              At,
  OUT VOID               *Buffer,
  IN UINTN               Bytes
  )
{
  EFI_STATUS  Status;
  UINTN       ReadSize;

  Status = Shell->SetFilePosition (File, At);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ReadSize = Bytes;
  Status = Shell->ReadFile (File, &ReadSize, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return (ReadSize == Bytes) ? EFI_SUCCESS : EFI_END_OF_FILE;
}

STATIC
VOID
PrintBasicBoot (
  IN CONST EFI_ACPI_5_0_FPDT_FIRMWARE_BASIC_BOOT_RECORD  *Boot
  )
{
  Print (L"  BasicBoot ResetEnd=%lu\n", Boot->ResetEnd);
  Print (L"  BasicBoot OsLoaderLoadImageStart=%lu\n",
         Boot->OsLoaderLoadImageStart);
  Print (L"  BasicBoot OsLoaderStartImageStart=%lu\n",
         Boot->OsLoaderStartImageStart);
  Print (L"  BasicBoot ExitBootServicesEntry=%lu\n",
         Boot->ExitBootServicesEntry);
  Print (L"  BasicBoot ExitBootServicesExit=%lu\n",
         Boot->ExitBootServicesExit);
  Print (L"  Units=FPDT raw timestamp values; do not infer OS POST from Shell data\n");

  if ((Boot->OsLoaderLoadImageStart == 0) ||
      (Boot->OsLoaderStartImageStart == 0) ||
      (Boot->ExitBootServicesEntry == 0) ||
      (Boot->ExitBootServicesExit == 0))
  {
    Print (L"  WindowsBootMetrics=N/A reason=OS_LOADER_TIMESTAMPS_INCOMPLETE\n");
  } else if ((Boot->OsLoaderStartImageStart < Boot->OsLoaderLoadImageStart) ||
             (Boot->ExitBootServicesEntry < Boot->OsLoaderStartImageStart) ||
             (Boot->ExitBootServicesExit < Boot->ExitBootServicesEntry))
  {
    Print (L"  WindowsBootMetrics=N/A reason=NON_MONOTONIC_TIMESTAMPS\n");
  } else {
    Print (L"  OsLoaderLoadToStartDelta=%lu (raw timestamp delta)\n",
           Boot->OsLoaderStartImageStart - Boot->OsLoaderLoadImageStart);
    Print (L"  ExitBootServicesDuration=%lu (raw timestamp delta)\n",
           Boot->ExitBootServicesExit - Boot->ExitBootServicesEntry);
  }
}

STATIC
EFI_STATUS
ParseFbpt (
  IN EFI_SHELL_PROTOCOL  *Shell,
  IN SHELL_FILE_HANDLE   File,
  IN UINT64              ActualLength
  )
{
  EFI_STATUS                                      Status;
  EFI_ACPI_5_0_FPDT_PERFORMANCE_TABLE_HEADER     Table;
  EFI_ACPI_5_0_FPDT_PERFORMANCE_RECORD_HEADER    Record;
  EFI_ACPI_5_0_FPDT_FIRMWARE_BASIC_BOOT_RECORD   Boot;
  UINT64                                         Offset;
  UINT32                                         Count;
  UINT32                                         BasicCount;
  UINT32                                         OtherCount;

  if ((ActualLength < sizeof (Table)) ||
      (ActualLength > DPX_MAX_FBPT_FILE_BYTES))
  {
    Print (L"FBPT status=invalid reason=FILE_SIZE_OUT_OF_RANGE\n");
    return EFI_BAD_BUFFER_SIZE;
  }

  Status = FpdtReadExact (Shell, File, 0, &Table, sizeof (Table));
  if (EFI_ERROR (Status)) {
    Print (L"FBPT status=invalid reason=TABLE_HEADER_UNREADABLE (%r)\n", Status);
    return Status;
  }

  if (Table.Signature != EFI_ACPI_5_0_FPDT_BOOT_PERFORMANCE_TABLE_SIGNATURE) {
    Print (L"FBPT status=invalid reason=BAD_SIGNATURE\n");
    return EFI_UNSUPPORTED;
  }

  if ((Table.Length < sizeof (Table)) ||
      (Table.Length > ActualLength) ||
      (Table.Length > DPX_MAX_FBPT_FILE_BYTES))
  {
    Print (L"FBPT status=invalid reason=BAD_TABLE_LENGTH\n");
    return EFI_VOLUME_CORRUPTED;
  }

  Print (L"FBPT signature=FBPT size=%u fileSize=%lu source=Shell file\n",
         Table.Length, ActualLength);
  if (Table.Length != ActualLength) {
    Print (L"  Warning=TRAILING_BYTES_IGNORED size=%lu\n",
           ActualLength - Table.Length);
  }

  Offset = sizeof (Table);
  Count = 0;
  BasicCount = 0;
  OtherCount = 0;
  while (Offset < Table.Length) {
    if ((UINT64)Table.Length - Offset < sizeof (Record)) {
      Print (L"FBPT status=invalid reason=TRUNCATED_RECORD_HEADER at=0x%lx\n", Offset);
      return EFI_VOLUME_CORRUPTED;
    }

    Status = FpdtReadExact (Shell, File, Offset, &Record, sizeof (Record));
    if (EFI_ERROR (Status)) {
      Print (L"FBPT status=invalid reason=RECORD_HEADER_READ_ERROR at=0x%lx\n", Offset);
      return Status;
    }

    if ((Record.Length < sizeof (Record)) ||
        (Record.Length > (UINT64)Table.Length - Offset))
    {
      Print (L"FBPT status=invalid reason=RECORD_LENGTH_INVALID at=0x%lx type=0x%04x len=%u\n",
             Offset, Record.Type, (UINT32)Record.Length);
      return EFI_VOLUME_CORRUPTED;
    }

    if (Count >= DPX_MAX_FBPT_RECORDS) {
      Print (L"FBPT status=invalid reason=RECORD_COUNT_LIMIT\n");
      return EFI_OUT_OF_RESOURCES;
    }

    if (Record.Type == EFI_ACPI_5_0_FPDT_RUNTIME_RECORD_TYPE_FIRMWARE_BASIC_BOOT) {
      if ((Record.Length < sizeof (Boot)) || (BasicCount > 0)) {
        Print (L"FBPT status=invalid reason=BASIC_BOOT_RECORD_SIZE_OR_DUPLICATE\n");
        return EFI_VOLUME_CORRUPTED;
      }

      Status = FpdtReadExact (Shell, File, Offset, &Boot, sizeof (Boot));
      if (EFI_ERROR (Status)) {
        Print (L"FBPT status=invalid reason=BASIC_BOOT_UNREADABLE\n");
        return Status;
      }

      if (Record.Revision != EFI_ACPI_5_0_FPDT_RUNTIME_RECORD_REVISION_FIRMWARE_BASIC_BOOT) {
        Print (L"FBPT status=invalid reason=UNSUPPORTED_BASIC_BOOT_REVISION\n");
        return EFI_UNSUPPORTED;
      }

      PrintBasicBoot (&Boot);
      ++BasicCount;
    } else {
      // Future or vendor-specific record types are preserved as a count and
      // safely skipped. This stage does not invent start/end pair semantics.
      ++OtherCount;
    }

    ++Count;
    Offset += Record.Length;
  }

  if (BasicCount == 0) {
    Print (L"FBPT status=partial reason=BASIC_BOOT_RECORD_MISSING\n");
    return EFI_NOT_FOUND;
  }

  Print (L"FBPT status=verified Records=%u BasicBootRecords=%u OtherRecords=%u\n",
         Count, BasicCount, OtherCount);
  // Validate extended event decoder against real UEFI Shell fixture files.
  // Keep guest allocations bounded independently of the 64 MiB file limit.
  if (Table.Length <= 2 * 1024 * 1024) {
    UINT8 *Raw;
    Raw = AllocatePool (Table.Length);
    if (Raw != NULL) {
      Status = FpdtReadExact (Shell, File, 0, Raw, Table.Length);
      if (!EFI_ERROR (Status)) {
        Status = DpXAnalyzeExtendedFbpt (Raw, Table.Length);
        if (EFI_ERROR (Status)) {
          Print (L"FBPT extended event records partial (%r)\n", Status);
        }
      }

      FreePool (Raw);
    }
  } else {
    Print (L"FBPT ExtendedEvents=N/A reason=OFFLINE_MEMORY_CAP_2MIB\n");
  }
  return EFI_SUCCESS;
}

EFI_STATUS
DpXParseFbptFile (
  IN CONST CHAR16  *Path
  )
{
  EFI_STATUS          Status;
  EFI_SHELL_PROTOCOL  *Shell;
  SHELL_FILE_HANDLE   File;
  UINT64              FileSize;

  if ((Path == NULL) || (Path[0] == L'\0')) {
    return EFI_INVALID_PARAMETER;
  }

  Shell = NULL;
  File = NULL;
  Status = gBS->LocateProtocol (
                  &gEfiShellProtocolGuid,
                  NULL,
                  (VOID **)&Shell
                  );
  if (EFI_ERROR (Status) || (Shell == NULL)) {
    Print (L"FBPT Shell access not available (%r)\n", Status);
    return EFI_UNSUPPORTED;
  }

  Status = Shell->OpenFileByName (Path, &File, EFI_FILE_MODE_READ);
  if (EFI_ERROR (Status)) {
    Print (L"FBPT input unavailable: %s (%r)\n", Path, Status);
    return Status;
  }

  FileSize = 0;
  Status = Shell->GetFileSize (File, &FileSize);
  if (!EFI_ERROR (Status)) {
    Status = ParseFbpt (Shell, File, FileSize);
  }

  Shell->CloseFile (File);
  return Status;
}
