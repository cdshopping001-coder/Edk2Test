/** @file
  Read-only live discovery of firmware-published ACPI FPDT.

  Uses TianoCore EfiLocateFirstAcpiTable, the same ACPI lookup path used by
  upstream Dp. Pointer records are diagnostics only; raw physical addresses
  are not dereferenced by this stage.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <IndustryStandard/Acpi50.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>

#define DPX_MAX_LIVE_FPDT_LENGTH  (1024U * 1024U)
#define DPX_MAX_LIVE_FPDT_RECORDS 4096

EFI_STATUS
DpXInspectLiveFpdt (
  VOID
  )
{
  EFI_ACPI_COMMON_HEADER                           *Common;
  EFI_ACPI_DESCRIPTION_HEADER                      *Acpi;
  EFI_ACPI_5_0_FPDT_PERFORMANCE_RECORD_HEADER      *Record;
  EFI_ACPI_5_0_FPDT_BOOT_PERFORMANCE_TABLE_POINTER_RECORD Boot;
  EFI_ACPI_5_0_FPDT_S3_PERFORMANCE_TABLE_POINTER_RECORD S3;
  CONST UINT8                                      *Bytes;
  UINT32                                           Offset;
  UINT32                                           Count;
  UINT32                                           BootCount;
  UINT32                                           S3Count;
  UINTN                                            I;
  UINT8                                            Checksum;

  Print (L"Live FPDT discovery source=firmware-published ACPI XSDT/RSDT\n");
  Common = EfiLocateFirstAcpiTable (
             EFI_ACPI_5_0_FIRMWARE_PERFORMANCE_DATA_TABLE_SIGNATURE
             );
  if (Common == NULL) {
    Print (L"Live FPDT status=unavailable reason=NOT_PRESENT_IN_ACPI\n");
    Print (L"  FBPT=N/A reason=NO_FPDT_TABLE\n");
    return EFI_NOT_FOUND;
  }

  // This pointer came from firmware's ACPI tables, NOT from user input.
  // Only access the bounded ACPI FPDT itself, never its embedded physical
  // Basic Boot / S3 pointers.
  Acpi = (EFI_ACPI_DESCRIPTION_HEADER *)(VOID *)Common;
  if ((Acpi->Signature != EFI_ACPI_5_0_FIRMWARE_PERFORMANCE_DATA_TABLE_SIGNATURE) ||
      (Acpi->Length < sizeof (EFI_ACPI_DESCRIPTION_HEADER)) ||
      (Acpi->Length > DPX_MAX_LIVE_FPDT_LENGTH))
  {
    Print (L"Live FPDT status=invalid reason=BAD_ACPI_HEADER_LENGTH\n");
    return EFI_VOLUME_CORRUPTED;
  }

  Bytes = (CONST UINT8 *)(CONST VOID *)Acpi;
  Checksum = 0;
  for (I = 0; I < Acpi->Length; ++I) {
    Checksum = (UINT8)(Checksum + Bytes[I]);
  }

  if (Checksum != 0) {
    Print (L"Live FPDT status=invalid reason=ACPI_CHECKSUM_INVALID\n");
    return EFI_CRC_ERROR;
  }

  Print (L"Live FPDT size=%u revision=%u checksum=valid\n",
         Acpi->Length, (UINT32)Acpi->Revision);
  Offset    = (UINT32)sizeof (EFI_ACPI_DESCRIPTION_HEADER);
  Count     = 0;
  BootCount = 0;
  S3Count   = 0;

  while (Offset < Acpi->Length) {
    if (Acpi->Length - Offset < sizeof (*Record)) {
      Print (L"Live FPDT status=invalid reason=TRUNCATED_RECORD_HEADER\n");
      return EFI_VOLUME_CORRUPTED;
    }

    Record = (EFI_ACPI_5_0_FPDT_PERFORMANCE_RECORD_HEADER *)(VOID *)(Bytes + Offset);
    if ((Record->Length < sizeof (*Record)) ||
        ((UINT32)Record->Length > Acpi->Length - Offset) ||
        (Count >= DPX_MAX_LIVE_FPDT_RECORDS))
    {
      Print (L"Live FPDT status=invalid reason=BAD_RECORD_LENGTH_OR_LIMIT\n");
      return EFI_VOLUME_CORRUPTED;
    }

    if (Record->Type == EFI_ACPI_5_0_FPDT_RECORD_TYPE_FIRMWARE_BASIC_BOOT_POINTER) {
      if ((Record->Length < sizeof (Boot)) ||
          (Record->Revision != EFI_ACPI_5_0_FPDT_RECORD_REVISION_FIRMWARE_BASIC_BOOT_POINTER) ||
          (BootCount != 0))
      {
        Print (L"Live FPDT status=invalid reason=BAD_BOOT_POINTER_RECORD\n");
        return EFI_VOLUME_CORRUPTED;
      }

      // Pointer record is packed. Copy rather than dereferencing a potentially
      // unaligned UINT64 address inside its structure.
      CopyMem (&Boot, Bytes + Offset, sizeof (Boot));
      Print (L"  BootPerformanceTablePointer=0x%lx source=ACPI pointer (NOT DEREFERENCED)\n",
             Boot.BootPerformanceTablePointer);
      ++BootCount;
    } else if (Record->Type == EFI_ACPI_5_0_FPDT_RECORD_TYPE_S3_PERFORMANCE_TABLE_POINTER) {
      if ((Record->Length < sizeof (S3)) ||
          (Record->Revision != EFI_ACPI_5_0_FPDT_RECORD_REVISION_S3_PERFORMANCE_TABLE_POINTER) ||
          (S3Count != 0))
      {
        Print (L"Live FPDT status=invalid reason=BAD_S3_POINTER_RECORD\n");
        return EFI_VOLUME_CORRUPTED;
      }

      CopyMem (&S3, Bytes + Offset, sizeof (S3));
      Print (L"  S3PerformanceTablePointer=0x%lx source=ACPI pointer (NOT DEREFERENCED)\n",
             S3.S3PerformanceTablePointer);
      ++S3Count;
    }

    Offset += Record->Length;
    ++Count;
  }

  Print (L"Live FPDT status=verified Records=%u BootPointers=%u S3Pointers=%u\n",
         Count, BootCount, S3Count);
  Print (L"  FBPT=N/A reason=LIVE_MEMORY_READ_NOT_YET_IMPLEMENTED\n");
  return EFI_SUCCESS;
}
