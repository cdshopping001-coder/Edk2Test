/** @file
  Portable, read-only ASCII tree prefix for UEFI consoles.
  Use |-- / `-- because firmware text renderers do not universally contain
  U+251C/U+2514 box-drawing glyphs.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "DpX.h"
#include <Library/UefiLib.h>

#define DPX_TREE_MAX_LEVEL 12

VOID
DpXTreePrefix (
  IN UINTN Level,
  IN BOOLEAN IsLast
  )
{
  UINTN Depth;
  if (Level > DPX_TREE_MAX_LEVEL) {
    Level = DPX_TREE_MAX_LEVEL;
  }
  for (Depth = 0; Depth < Level; ++Depth) {
    Print (L"|   ");
  }
  Print (IsLast ? L"`-- " : L"|-- ");
}


/**
  Emit a multi-level console tree. A set bit at level I means its ancestor
  branch ended, so subsequent prefixes use spaces instead of spurious pipes.
**/
VOID
DpXTreePath (
  IN UINTN   Level,
  IN UINT32  AncestorLast,
  IN BOOLEAN IsLast
  )
{
  UINTN I;
  if (Level > 12) {
    Level = 12;
  }
  for (I = 0; I < Level; ++I) {
    Print ((AncestorLast & (1U << I)) != 0 ? L"    " : L"|   ");
  }
  Print (IsLast ? L"`-- " : L"|-- ");
}
