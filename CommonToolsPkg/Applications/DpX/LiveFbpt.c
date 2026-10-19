/** @file
  Explicit opt-in read of a firmware-published FBPT physical address.

  The address comes only from an ACPI FPDT whose checksum/record layout was
  validated by LiveFpdt.c. Never read a user-supplied address. The complete
  requested address range must belong to a single UEFI Memory Map descriptor
  of an allowed RAM-backed type before dereferencing any byte.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <IndustryStandard/Acpi50.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define DPX_FBPT_MAX_BYTES          (16U * 1024U * 1024U)
#define DPX_FBPT_MAX_MEMORY_MAP     (8U * 1024U * 1024U)
#define DPX_FBPT_MEMORY_MAP_SPARE   (16U * 1024U)
#define DPX_FBPT_RECORD_LIMIT       131072

STATIC
EFI_STATUS
GetBootMemoryMap (
  OUT EFI_MEMORY_DESCRIPTOR  **MapOut,
  OUT UINTN                  *MapBytes,
  OUT UINTN                  *DescriptorBytes
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  UINTN                  Size;
  UINTN                  MapKey;
  UINTN                  DescriptorSize;
  UINT32                 Version;

  if ((MapOut == NULL) || (MapBytes == NULL) || (DescriptorBytes == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *MapOut = NULL;
  Size = 0;
  DescriptorSize = 0;
  MapKey = 0;
  Version = 0;
  Status = gBS->GetMemoryMap (&Size, NULL, &MapKey, &DescriptorSize, &Version);
  if ((Status != EFI_BUFFER_TOO_SMALL) ||
      (DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR)) ||
      (Size > DPX_FBPT_MAX_MEMORY_MAP - DPX_FBPT_MEMORY_MAP_SPARE))
  {
    return EFI_UNSUPPORTED;
  }

  Size += DPX_FBPT_MEMORY_MAP_SPARE;
  Map = AllocatePool (Size);
  if (Map == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->GetMemoryMap (&Size, Map, &MapKey, &DescriptorSize, &Version);
  if (EFI_ERROR (Status) ||
      (DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR)) ||
      ((Size % DescriptorSize) != 0))
  {
    FreePool (Map);
    return EFI_DEVICE_ERROR;
  }

  *MapOut = Map;
  *MapBytes = Size;
  *DescriptorBytes = DescriptorSize;
  return EFI_SUCCESS;
}

/**
  No arbitrary MMIO, Unusable Memory, SMRAM, or unclassified pointers.
  TianoCore FirmwarePerformanceDxe allocates FBPT with EfiReservedMemoryType;
  ACPI reclaim/NVS and boot-services data may also contain firmware data.
**/
STATIC
BOOLEAN
IsAllowedRam (
  IN EFI_MEMORY_TYPE  Type
  )
{
  return (Type == EfiReservedMemoryType) ||
         (Type == EfiACPIReclaimMemory) ||
         (Type == EfiACPIMemoryNVS) ||
         (Type == EfiBootServicesData);
}

STATIC
BOOLEAN
RangeInAllowedMap (
  IN UINT64                       Address,
  IN UINT64                       Length,
  IN CONST EFI_MEMORY_DESCRIPTOR  *Map,
  IN UINTN                        MapBytes,
  IN UINTN                        DescriptorBytes
  )
{
  UINTN                        Offset;
  CONST EFI_MEMORY_DESCRIPTOR  *Descriptor;
  UINT64                       Start;
  UINT64                       Extent;
  UINT64                       End;

  if ((Address == 0) || (Length == 0) ||
      (Length > MAX_UINT64 - Address) ||
      (Address > MAX_UINTN) ||
      (Map == NULL) ||
      (DescriptorBytes < sizeof (EFI_MEMORY_DESCRIPTOR)))
  {
    return FALSE;
  }

  for (Offset = 0; Offset + DescriptorBytes <= MapBytes; Offset += DescriptorBytes) {
    Descriptor = (CONST EFI_MEMORY_DESCRIPTOR *)(
                   (CONST UINT8 *)Map + Offset
                   );
    if (!IsAllowedRam (Descriptor->Type) ||
        (Descriptor->NumberOfPages == 0) ||
        (Descriptor->NumberOfPages > MAX_UINT64 / EFI_PAGE_SIZE))
    {
      continue;
    }

    Start = Descriptor->PhysicalStart;
    Extent = Descriptor->NumberOfPages * EFI_PAGE_SIZE;
    if (Start > MAX_UINT64 - Extent) {
      continue;
    }

    End = Start + Extent;
    if ((Address >= Start) &&
        (Address < End) &&
        (Length <= End - Address))
    {
      return TRUE;
    }
  }

  return FALSE;
}

EFI_STATUS
DpXReadLiveFbpt (
  IN UINT64  BootPhysicalAddress
  )
{
  EFI_STATUS                                   Status;
  EFI_MEMORY_DESCRIPTOR                       *Map;
  UINTN                                        MapBytes;
  UINTN                                        DescriptorBytes;
  EFI_ACPI_5_0_FPDT_PERFORMANCE_TABLE_HEADER  Header;
  EFI_ACPI_5_0_FPDT_PERFORMANCE_RECORD_HEADER Record;
  EFI_ACPI_5_0_FPDT_FIRMWARE_BASIC_BOOT_RECORD Basic;
  CONST UINT8                                  *Data;
  UINT64                                       Offset;
  UINT32                                       Count;
  UINT32                                       BasicCount;

  Print (L"Live FBPT read source=validated firmware ACPI pointer + UEFI Memory Map\n");
  Map = NULL;
  MapBytes = 0;
  DescriptorBytes = 0;

  if (BootPhysicalAddress == 0) {
    Print (L"Live FBPT status=unavailable reason=ZERO_BOOT_POINTER\n");
    return EFI_NOT_FOUND;
  }

  Status = GetBootMemoryMap (&Map, &MapBytes, &DescriptorBytes);
  if (EFI_ERROR (Status)) {
    Print (L"Live FBPT status=unavailable reason=MEMORY_MAP_UNAVAILABLE (%r)\n", Status);
    return Status;
  }

  if (!RangeInAllowedMap (BootPhysicalAddress, sizeof (Header),
                          Map, MapBytes, DescriptorBytes))
  {
    Print (L"Live FBPT status=unavailable reason=HEADER_OUTSIDE_ALLOWED_MEMORY\n");
    Status = EFI_ACCESS_DENIED;
    goto Done;
  }

  // It is safe to read the fixed header only after validating its mapping.
  Data = (CONST UINT8 *)(UINTN)BootPhysicalAddress;
  CopyMem (&Header, Data, sizeof (Header));

  if ((Header.Signature != EFI_ACPI_5_0_FPDT_BOOT_PERFORMANCE_TABLE_SIGNATURE) ||
      (Header.Length < sizeof (Header) + sizeof (Basic)) ||
      (Header.Length > DPX_FBPT_MAX_BYTES))
  {
    Print (L"Live FBPT status=invalid reason=BAD_FBPT_HEADER\n");
    Status = EFI_VOLUME_CORRUPTED;
    goto Done;
  }

  if (!RangeInAllowedMap (BootPhysicalAddress, Header.Length,
                          Map, MapBytes, DescriptorBytes))
  {
    Print (L"Live FBPT status=unavailable reason=TABLE_OUTSIDE_ALLOWED_MEMORY\n");
    Status = EFI_ACCESS_DENIED;
    goto Done;
  }

  Print (L"Live FBPT size=%u range=validated\n", Header.Length);
  Offset = sizeof (Header);
  Count = 0;
  BasicCount = 0;
  while (Offset < Header.Length) {
    if (Header.Length - Offset < sizeof (Record)) {
      Print (L"Live FBPT status=invalid reason=TRUNCATED_RECORD_HEADER\n");
      Status = EFI_VOLUME_CORRUPTED;
      goto Done;
    }

    CopyMem (&Record, Data + Offset, sizeof (Record));
    if ((Record.Length < sizeof (Record)) ||
        (Record.Length > Header.Length - Offset) ||
        (Count >= DPX_FBPT_RECORD_LIMIT))
    {
      Print (L"Live FBPT status=invalid reason=RECORD_LENGTH_OR_LIMIT\n");
      Status = EFI_VOLUME_CORRUPTED;
      goto Done;
    }

    if (Record.Type == EFI_ACPI_5_0_FPDT_RUNTIME_RECORD_TYPE_FIRMWARE_BASIC_BOOT) {
      if ((Record.Length < sizeof (Basic)) ||
          (Record.Revision != EFI_ACPI_5_0_FPDT_RUNTIME_RECORD_REVISION_FIRMWARE_BASIC_BOOT) ||
          (BasicCount != 0))
      {
        Print (L"Live FBPT status=invalid reason=BASIC_BOOT_FORMAT_OR_DUPLICATE\n");
        Status = EFI_VOLUME_CORRUPTED;
        goto Done;
      }

      CopyMem (&Basic, Data + Offset, sizeof (Basic));
      Print (L"  Live BasicBoot ResetEnd=%lu (FPDT raw timestamp)\n", Basic.ResetEnd);
      Print (L"  Live BasicBoot OsLoaderLoadImageStart=%lu\n",
             Basic.OsLoaderLoadImageStart);
      Print (L"  Live BasicBoot OsLoaderStartImageStart=%lu\n",
             Basic.OsLoaderStartImageStart);
      Print (L"  Live BasicBoot ExitBootServicesEntry=%lu\n",
             Basic.ExitBootServicesEntry);
      Print (L"  Live BasicBoot ExitBootServicesExit=%lu\n",
             Basic.ExitBootServicesExit);
      if ((Basic.OsLoaderLoadImageStart == 0) ||
          (Basic.OsLoaderStartImageStart == 0) ||
          (Basic.ExitBootServicesEntry == 0) ||
          (Basic.ExitBootServicesExit == 0))
      {
        Print (L"  WindowsBootMetrics=N/A reason=OS_LOADER_NOT_REACHED_IN_SHELL\n");
      } else if ((Basic.OsLoaderStartImageStart < Basic.OsLoaderLoadImageStart) ||
                 (Basic.ExitBootServicesEntry < Basic.OsLoaderStartImageStart) ||
                 (Basic.ExitBootServicesExit < Basic.ExitBootServicesEntry))
      {
        Print (L"  WindowsBootMetrics=N/A reason=NON_MONOTONIC_TIMESTAMPS\n");
      } else {
        Print (L"  ExitBootServicesDuration=%lu (raw FPDT delta)\n",
               Basic.ExitBootServicesExit - Basic.ExitBootServicesEntry);
      }

      ++BasicCount;
    }

    Offset += Record.Length;
    ++Count;
  }

  if (BasicCount != 1) {
    Print (L"Live FBPT status=unavailable reason=BASIC_BOOT_RECORD_NOT_FOUND\n");
    Status = EFI_NOT_FOUND;
  } else {
    Print (L"Live FBPT status=verified Records=%u BasicBootRecords=%u\n",
           Count, BasicCount);
    Status = DpXAnalyzeExtendedFbpt (Data, Header.Length);
    if (EFI_ERROR (Status)) {
      Print (L"  ExtendedEvents=PARTIAL (%r); BasicBoot remains valid\n", Status);
      Status = EFI_SUCCESS;
    }
  }

Done:
  FreePool (Map);
  return Status;
}
