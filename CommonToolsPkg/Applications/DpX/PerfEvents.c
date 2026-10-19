/** @file
  Bounded EDK2 Extended FPDT event-pair analyzer.
  Reports measured inclusive intervals; it never infers unlogged call trees.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"

#include <Guid/ExtendedFirmwarePerformance.h>
#include <IndustryStandard/Acpi50.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PerformanceLib.h>
#include <Library/UefiLib.h>

#define DPX_EVENT_MAX_OPEN  512
#define DPX_EVENT_TOP       10
#define DPX_EVENT_MAX_COUNT 131072

typedef struct {
  BOOLEAN  Occupied;
  UINT16   ProgressId;
  UINT16   RecordType;
  UINT32   ApicId;
  UINT64   Timestamp;
  EFI_GUID ModuleGuid;
  EFI_GUID EventGuid;
  CHAR8    Token[48];
} DPX_EXT_EVENT;

typedef struct {
  UINT64        Duration;
  DPX_EXT_EVENT Start;
} DPX_EXT_TOP;

STATIC
UINT16
ExpectedEnd (
  IN UINT16  Start
  )
{
  switch (Start) {
    case MODULE_START_ID:           return MODULE_END_ID;
    case MODULE_LOADIMAGE_START_ID: return MODULE_LOADIMAGE_END_ID;
    case MODULE_DB_START_ID:        return MODULE_DB_END_ID;
    case MODULE_DB_SUPPORT_START_ID: return MODULE_DB_SUPPORT_END_ID;
    case MODULE_DB_STOP_START_ID:   return MODULE_DB_STOP_END_ID;
    case PERF_EVENTSIGNAL_START_ID: return PERF_EVENTSIGNAL_END_ID;
    case PERF_CALLBACK_START_ID:    return PERF_CALLBACK_END_ID;
    case PERF_FUNCTION_START_ID:    return PERF_FUNCTION_END_ID;
    case PERF_INMODULE_START_ID:    return PERF_INMODULE_END_ID;
    case PERF_CROSSMODULE_START_ID: return PERF_CROSSMODULE_END_ID;
    default:                        return 0;
  }
}

STATIC
BOOLEAN
KnownEnd (
  IN UINT16 Id
  )
{
  switch (Id) {
    case MODULE_END_ID:
    case MODULE_LOADIMAGE_END_ID:
    case MODULE_DB_END_ID:
    case MODULE_DB_SUPPORT_END_ID:
    case MODULE_DB_STOP_END_ID:
    case PERF_EVENTSIGNAL_END_ID:
    case PERF_CALLBACK_END_ID:
    case PERF_FUNCTION_END_ID:
    case PERF_INMODULE_END_ID:
    case PERF_CROSSMODULE_END_ID:
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC
VOID
ReadToken (
  OUT CHAR8       *Dest,
  IN UINTN        Capacity,
  IN CONST UINT8  *Bytes,
  IN UINTN        Size
  )
{
  UINTN I;
  ZeroMem (Dest, Capacity);
  for (I = 0; (I + 1 < Capacity) && (I < Size) && (Bytes[I] != 0); ++I) {
    Dest[I] = ((Bytes[I] >= 0x20) && (Bytes[I] < 0x7f)) ?
              (CHAR8)Bytes[I] : '?';
  }
}

STATIC
BOOLEAN
ReadEvent (
  IN CONST UINT8   *Raw,
  IN UINTN         Size,
  OUT DPX_EXT_EVENT *Event
  )
{
  FPDT_GUID_EVENT_RECORD  Base;
  UINTN                  TokenAt;

  if (Size < sizeof (Base)) {
    return FALSE;
  }

  ZeroMem (Event, sizeof (*Event));
  CopyMem (&Base, Raw, sizeof (Base));
  Event->ProgressId = Base.ProgressID;
  Event->RecordType = Base.Header.Type;
  Event->ApicId = Base.ApicID;
  Event->Timestamp = Base.Timestamp;
  Event->ModuleGuid = Base.Guid;
  TokenAt = 0;

  switch (Base.Header.Type) {
    case FPDT_GUID_EVENT_TYPE:
      break;
    case FPDT_DYNAMIC_STRING_EVENT_TYPE:
      TokenAt = sizeof (FPDT_GUID_EVENT_RECORD);
      break;
    case FPDT_DUAL_GUID_STRING_EVENT_TYPE:
      if (Size < OFFSET_OF (FPDT_DUAL_GUID_STRING_EVENT_RECORD, String)) {
        return FALSE;
      }

      CopyMem (&Event->EventGuid,
               Raw + OFFSET_OF (FPDT_DUAL_GUID_STRING_EVENT_RECORD, Guid2),
               sizeof (EFI_GUID));
      TokenAt = OFFSET_OF (FPDT_DUAL_GUID_STRING_EVENT_RECORD, String);
      break;
    case FPDT_GUID_QWORD_EVENT_TYPE:
      if (Size < sizeof (FPDT_GUID_QWORD_EVENT_RECORD)) {
        return FALSE;
      }

      break;
    case FPDT_GUID_QWORD_STRING_EVENT_TYPE:
      if (Size < sizeof (FPDT_GUID_QWORD_STRING_EVENT_RECORD)) {
        return FALSE;
      }

      TokenAt = sizeof (FPDT_GUID_QWORD_STRING_EVENT_RECORD);
      break;
    default:
      return FALSE;
  }

  if (TokenAt != 0 && Size > TokenAt) {
    ReadToken (Event->Token, sizeof (Event->Token),
               Raw + TokenAt, Size - TokenAt);
  }

  return TRUE;
}

STATIC
BOOLEAN
SameEventKey (
  IN CONST DPX_EXT_EVENT *Start,
  IN CONST DPX_EXT_EVENT *End
  )
{
  return Start->RecordType == End->RecordType &&
         Start->ApicId == End->ApicId &&
         CompareGuid (&Start->ModuleGuid, &End->ModuleGuid) &&
         CompareGuid (&Start->EventGuid, &End->EventGuid) &&
         AsciiStrCmp (Start->Token, End->Token) == 0 &&
         ExpectedEnd (Start->ProgressId) == End->ProgressId;
}

EFI_STATUS
DpXAnalyzeExtendedFbpt (
  IN CONST UINT8 *Data,
  IN UINTN       Available
  )
{
  EFI_ACPI_5_0_FPDT_PERFORMANCE_TABLE_HEADER  Table;
  EFI_ACPI_5_0_FPDT_PERFORMANCE_RECORD_HEADER Header;
  DPX_EXT_EVENT *Open;
  DPX_EXT_EVENT Event;
  DPX_EXT_TOP Top[DPX_EVENT_TOP];
  UINTN At;
  UINTN N;
  UINTN J;
  UINTN Slot;
  UINTN OpenCount;
  UINTN Records;
  UINTN Events;
  UINTN Paired;
  UINTN Orphan;
  UINTN Invalid;
  UINT64 Duration;
  CHAR16 Ui[128];
  UINTN UiMatches;
  EFI_STATUS Status;

  if (Data == NULL || Available < sizeof (Table)) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (&Table, Data, sizeof (Table));
  if (Table.Signature != EFI_ACPI_5_0_FPDT_BOOT_PERFORMANCE_TABLE_SIGNATURE ||
      Table.Length > Available || Table.Length < sizeof (Table)) {
    return EFI_VOLUME_CORRUPTED;
  }

  Open = AllocateZeroPool (DPX_EVENT_MAX_OPEN * sizeof (*Open));
  if (Open == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Top, sizeof (Top));
  At = sizeof (Table);
  Records = 0;
  Events = 0;
  Paired = 0;
  Orphan = 0;
  Invalid = 0;
  Status = EFI_SUCCESS;

  while (At < Table.Length) {
    if ((Table.Length - At < sizeof (Header)) ||
        (Records >= DPX_EVENT_MAX_COUNT)) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    CopyMem (&Header, Data + At, sizeof (Header));
    if ((Header.Length < sizeof (Header)) ||
        (Header.Length > Table.Length - At)) {
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    ++Records;
    if (Header.Type >= FPDT_GUID_EVENT_TYPE &&
        Header.Type <= FPDT_GUID_QWORD_STRING_EVENT_TYPE) {
      if (!ReadEvent (Data + At, Header.Length, &Event)) {
        ++Invalid;
      } else {
        ++Events;
        if (ExpectedEnd (Event.ProgressId) != 0) {
          for (Slot = 0; Slot < DPX_EVENT_MAX_OPEN; ++Slot) {
            if (!Open[Slot].Occupied) {
              Open[Slot] = Event;
              Open[Slot].Occupied = TRUE;
              break;
            }
          }

          if (Slot == DPX_EVENT_MAX_OPEN) {
            Status = EFI_OUT_OF_RESOURCES;
            break;
          }
        } else if (KnownEnd (Event.ProgressId)) {
          // Match last-started compatible record. Unmatched and crossed
          // intervals remain non-causal; no "exclusive time" inferred.
          for (Slot = DPX_EVENT_MAX_OPEN; Slot > 0; --Slot) {
            N = Slot - 1;
            if (Open[N].Occupied && SameEventKey (&Open[N], &Event)) {
              break;
            }
          }

          if (Slot == 0) {
            ++Orphan;
          } else {
            N = Slot - 1;
            if (Event.Timestamp < Open[N].Timestamp) {
              ++Invalid;
            } else {
              Duration = Event.Timestamp - Open[N].Timestamp;
              ++Paired;
              for (J = 0; J < DPX_EVENT_TOP; ++J) {
                if (Duration > Top[J].Duration) {
                  UINTN K;
                  for (K = DPX_EVENT_TOP - 1; K > J; --K) {
                    Top[K] = Top[K - 1];
                  }

                  Top[J].Duration = Duration;
                  Top[J].Start = Open[N];
                  break;
                }
              }
            }

            Open[N].Occupied = FALSE;
          }
        }
      }
    }

    At += Header.Length;
  }

  OpenCount = 0;
  for (N = 0; N < DPX_EVENT_MAX_OPEN; ++N) {
    if (Open[N].Occupied) {
      ++OpenCount;
    }
  }

  Print (L"DPX Extended FPDT: Records=%u Events=%u Pairs=%u Unclosed=%u OrphanEnds=%u Invalid=%u\n",
         (UINT32)Records, (UINT32)Events, (UINT32)Paired,
         (UINT32)OpenCount, (UINT32)Orphan, (UINT32)Invalid);
  if (EFI_ERROR (Status)) {
    Print (L"  EventStatus=PARTIAL reason=CORRUPT_RECORD_OR_LIMIT (%r)\n", Status);
  }

  if (Paired == 0) {
    Print (L"  EventTop=N/A reason=NO_MATCHED_EXTENDED_RECORDS\n");
  }

  for (J = 0; J < DPX_EVENT_TOP && Top[J].Duration != 0; ++J) {
    ZeroMem (Ui, sizeof (Ui));
    UiMatches = 0;
    DpXLookupModuleUi (&Top[J].Start.ModuleGuid, Ui,
                       sizeof (Ui) / sizeof (Ui[0]), &UiMatches);
    Print (L"  Top[%u] DurationNs=%lu Progress=0x%04x ModuleGuid=%g Module=%s Confidence=%s\n",
           (UINT32)(J + 1), Top[J].Duration, Top[J].Start.ProgressId,
           &Top[J].Start.ModuleGuid,
           UiMatches == 1 && Ui[0] != 0 ? Ui : L"N/A",
           UiMatches == 1 ? L"FFS_GUID_MATCH" : (UiMatches > 1 ? L"AMBIGUOUS" : L"UNRESOLVED"));
    if (Top[J].Start.RecordType == FPDT_DUAL_GUID_STRING_EVENT_TYPE) {
      Print (L"    TriggerGuid=%g (Event/PPI/Protocol; NOT FFS module)\n",
             &Top[J].Start.EventGuid);
    }

    Print (L"    Token=%a LabelSource=FPDT_RECORD_STRING FunctionSymbol=%s\n",
           Top[J].Start.Token,
           Top[J].Start.Token[0] != 0 ? L"UNVERIFIED_TOKEN" : L"N/A");
  }

  Print (L"  TimingSemantics=inclusive intervals; no call tree or exclusive attribution inferred\n");
  FreePool (Open);
  return Status;
}
