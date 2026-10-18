/** @file
  Read-only extended EDK2 FPDT event-pair summary.
  Reports measured intervals only. Unpaired/ambiguous events are NOT elapsed time.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"
#include <Guid/ExtendedFirmwarePerformance.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/PerformanceLib.h>
#include <IndustryStandard/Acpi50.h>

#define DPX_OPEN_PAIRS  2048
#define DPX_TOP_PAIRS   12
#define DPX_EVENT_LIMIT 131072

typedef struct {
  BOOLEAN Open;
  UINT16 StartId;
  UINT16 Type;
  UINT32 ApicId;
  EFI_GUID ModuleGuid;
  EFI_GUID EventGuid;
  UINT64 Time;
  CHAR8 Token[32];
} DPX_OPEN_EVENT;

typedef struct {
  UINT64 Ns;
  UINT16 Progress;
  EFI_GUID ModuleGuid;
  CHAR8 Token[32];
} DPX_TOP_EVENT;

STATIC
UINT16
PairedEnd (
  IN UINT16 Start
  )
{
  switch (Start) {
    case MODULE_START_ID:         return MODULE_END_ID;
    case 0x03:                    return 0x04;
    case 0x05:                    return 0x06;
    case 0x07:                    return 0x08;
    case 0x09:                    return 0x0A;
    case PERF_EVENTSIGNAL_START_ID: return PERF_EVENTSIGNAL_END_ID;
    case PERF_CALLBACK_START_ID:  return PERF_CALLBACK_END_ID;
    case 0x30:                    return 0x31;
    case PERF_INMODULE_START_ID:  return PERF_INMODULE_END_ID;
    case PERF_CROSSMODULE_START_ID: return PERF_CROSSMODULE_END_ID;
    default:                      return 0;
  }
}

STATIC
VOID
CopySafeToken (
  OUT CHAR8        *Destination,
  IN UINTN         Max,
  IN CONST UINT8   *Data,
  IN UINTN         Bytes
  )
{
  UINTN I;
  if (Max == 0) {
    return;
  }

  for (I = 0; (I + 1 < Max) && (I < Bytes); ++I) {
    if (Data[I] == 0) {
      break;
    }

    Destination[I] = (Data[I] >= 32 && Data[I] <= 126) ?
                     (CHAR8)Data[I] : '?';
  }

  Destination[I] = 0;
}

STATIC
BOOLEAN
DecodeEvent (
  IN CONST UINT8  *Raw,
  IN UINT8        Length,
  OUT DPX_OPEN_EVENT *Out
  )
{
  FPDT_GUID_EVENT_RECORD  Event;
  UINTN Minimum;
  UINTN TokenStart;

  // Every supported EDK2 event has the same fixed ProgressID, APIC ID,
  // Timestamp and first GUID fields as FPDT_GUID_EVENT_RECORD.
  Minimum = sizeof (FPDT_GUID_EVENT_RECORD);
  if (Length < Minimum) {
    return FALSE;
  }

  ZeroMem (Out, sizeof (*Out));
  CopyMem (&Event, Raw, sizeof (Event));
  Out->StartId = Event.ProgressID;
  Out->Type = Event.Header.Type;
  Out->ApicId = Event.ApicID;
  Out->ModuleGuid = Event.Guid;
  Out->Time = Event.Timestamp;

  TokenStart = Minimum;
  if (Event.Header.Type == FPDT_DUAL_GUID_STRING_EVENT_TYPE) {
    if (Length < OFFSET_OF (FPDT_DUAL_GUID_STRING_EVENT_RECORD, String)) {
      return FALSE;
    }

    CopyMem (&Out->EventGuid,
             Raw + OFFSET_OF (FPDT_DUAL_GUID_STRING_EVENT_RECORD, Guid2),
             sizeof (EFI_GUID));
    TokenStart = OFFSET_OF (FPDT_DUAL_GUID_STRING_EVENT_RECORD, String);
  } else if (Event.Header.Type == FPDT_GUID_QWORD_EVENT_TYPE ||
             Event.Header.Type == FPDT_GUID_QWORD_STRING_EVENT_TYPE)
  {
    if (Length < sizeof (FPDT_GUID_QWORD_EVENT_RECORD)) {
      return FALSE;
    }

    TokenStart = sizeof (FPDT_GUID_QWORD_EVENT_RECORD);
  }

  if (Event.Header.Type == FPDT_DYNAMIC_STRING_EVENT_TYPE ||
      Event.Header.Type == FPDT_DUAL_GUID_STRING_EVENT_TYPE ||
      Event.Header.Type == FPDT_GUID_QWORD_STRING_EVENT_TYPE)
  {
    if (Length > TokenStart) {
      CopySafeToken (Out->Token, sizeof (Out->Token),
                     Raw + TokenStart, Length - TokenStart);
    }
  }

  return TRUE;
}

STATIC
VOID
RecordTop (
  IN OUT DPX_TOP_EVENT      *Top,
  IN UINTN                  Slots,
  IN CONST DPX_OPEN_EVENT   *Event,
  IN UINT64                 Duration
  )
{
  UINTN I;
  UINTN J;
  if (Duration == 0) {
    return;
  }

  for (I = 0; I < Slots; ++I) {
    if (Duration > Top[I].Ns) {
      for (J = Slots - 1; J > I; --J) {
        Top[J] = Top[J - 1];
      }

      Top[I].Ns = Duration;
      Top[I].Progress = Event->StartId;
      Top[I].ModuleGuid = Event->ModuleGuid;
      CopyMem (Top[I].Token, Event->Token, sizeof (Top[I].Token));
      break;
    }
  }
}

STATIC
BOOLEAN
SameKey (
  IN CONST DPX_OPEN_EVENT  *Left,
  IN CONST DPX_OPEN_EVENT  *Right
  )
{
  // Do not match different architectures/record encodings together.
  return (Left->Type == Right->Type) &&
         (Left->ApicId == Right->ApicId) &&
         CompareGuid (&Left->ModuleGuid, &Right->ModuleGuid) &&
         CompareGuid (&Left->EventGuid, &Right->EventGuid) &&
         (AsciiStrCmp (Left->Token, Right->Token) == 0);
}

EFI_STATUS
DpXSummarizeFpdtEvents (
  IN CONST UINT8  *Data,
  IN UINT32       TableBytes
  )
{
  EFI_STATUS Status;
  DPX_OPEN_EVENT *Open;
  DPX_OPEN_EVENT Event;
  DPX_TOP_EVENT Top[DPX_TOP_PAIRS];
  EFI_ACPI_5_0_FPDT_PERFORMANCE_TABLE_HEADER Table;
  EFI_ACPI_5_0_FPDT_PERFORMANCE_RECORD_HEADER Header;
  UINTN I;
  UINTN Best;
  UINTN FreeSlot;
  UINT32 Offset;
  UINT32 Entries;
  UINT32 Events;
  UINT32 Pairs;
  UINT32 UnpairedEnd;
  UINT32 Unknown;
  UINT32 Bad;
  UINT32 UnpairedStarts;
  UINT64 Elapsed;
  UINT16 EndId;
  BOOLEAN Found;

  if (Data == NULL || TableBytes < sizeof (Table)) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (&Table, Data, sizeof (Table));
  if (Table.Signature != EFI_ACPI_5_0_FPDT_BOOT_PERFORMANCE_TABLE_SIGNATURE ||
      Table.Length > TableBytes ||
      Table.Length < sizeof (Table))
  {
    return EFI_VOLUME_CORRUPTED;
  }

  Open = AllocateZeroPool (DPX_OPEN_PAIRS * sizeof (*Open));
  if (Open == NULL) {
    Print (L"FPDT extended event summary unavailable: OUT_OF_RESOURCES\n");
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Top, sizeof (Top));
  Offset = sizeof (Table);
  Entries = 0;
  Events = 0;
  Pairs = 0;
  UnpairedEnd = 0;
  Unknown = 0;
  Bad = 0;
  Status = EFI_SUCCESS;

  while (Offset < Table.Length) {
    if ((Table.Length - Offset < sizeof (Header)) ||
        (Entries >= DPX_EVENT_LIMIT))
    {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    CopyMem (&Header, Data + Offset, sizeof (Header));
    if (Header.Length < sizeof (Header) ||
        Header.Length > Table.Length - Offset)
    {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    if (Header.Type >= FPDT_GUID_EVENT_TYPE &&
        Header.Type <= FPDT_GUID_QWORD_STRING_EVENT_TYPE)
    {
      if (!DecodeEvent (Data + Offset, Header.Length, &Event)) {
        ++Bad;
      } else {
        ++Events;
        EndId = PairedEnd (Event.StartId);
        if (EndId != 0) {
          FreeSlot = DPX_OPEN_PAIRS;
          for (I = 0; I < DPX_OPEN_PAIRS; ++I) {
            if (!Open[I].Open) {
              FreeSlot = I;
              break;
            }
          }

          if (FreeSlot == DPX_OPEN_PAIRS) {
            Print (L"FPDT events truncated: open-pair limit hit\n");
            Status = EFI_OUT_OF_RESOURCES;
            break;
          }

          Open[FreeSlot] = Event;
          Open[FreeSlot].Open = TRUE;
        } else {
          Found = FALSE;
          for (I = DPX_OPEN_PAIRS; I > 0; --I) {
            // Reverse scan by slot alone is a bounded heuristic, not a
            // guarantee of call-stack nesting across different CPU streams.
            Best = I - 1;
            if (!Open[Best].Open ||
                PairedEnd (Open[Best].StartId) != Event.StartId ||
                !SameKey (&Open[Best], &Event))
            {
              continue;
            }

            Found = TRUE;
            if (Event.Time >= Open[Best].Time) {
              Elapsed = Event.Time - Open[Best].Time;
              RecordTop (Top, DPX_TOP_PAIRS, &Open[Best], Elapsed);
              ++Pairs;
            } else {
              ++Bad;
            }

            Open[Best].Open = FALSE;
            break;
          }

          if (!Found) {
            ++UnpairedEnd;
          }
        }
      }
    } else if (Header.Type != EFI_ACPI_5_0_FPDT_RUNTIME_RECORD_TYPE_FIRMWARE_BASIC_BOOT) {
      ++Unknown;
    }

    Offset += Header.Length;
    ++Entries;
  }

  UnpairedStarts = 0;
  for (I = 0; I < DPX_OPEN_PAIRS; ++I) {
    if (Open[I].Open) {
      ++UnpairedStarts;
    }
  }

  if (EFI_ERROR (Status)) {
    Print (L"FPDT events status=partial reason=CORRUPT_RECORD_OR_LIMIT\n");
  } else {
    Print (L"FPDT events status=parsed Source=FBPT elapsedUnit=ns\n");
  }

  Print (L"FPDT events Records=%u Decoded=%u Paired=%u Open=%u UnpairedEnds=%u Unsupported=%u Invalid=%u\n",
         Entries, Events, Pairs, UnpairedStarts, UnpairedEnd, Unknown, Bad);
  Print (L"FPDT longest measured intervals (inclusive only; no exclusive/gap inference):\n");
  for (I = 0; I < DPX_TOP_PAIRS && Top[I].Ns != 0; ++I) {
    Print (L"  Top[%u] DurationNs=%lu ProgressStart=0x%04x ModuleGuid=%g Token=%a\n",
           (UINT32)(I + 1), Top[I].Ns, Top[I].Progress,
           &Top[I].ModuleGuid, Top[I].Token);
  }

  FreePool (Open);
  return Status;
}
