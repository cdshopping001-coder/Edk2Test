#!/usr/bin/env python3
"""Check actual redirected UEFI Shell output, not a host reimplementation."""
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
output = data.decode("utf-16le", errors="replace") if b"\0" in data else data.decode("utf-8", errors="replace")
required = (
    "DPX_SMOKE_BEGIN", "DPX_SMOKE_DONE", "DpX 0.2.0",
    "Total=4096", "Used=136", "Free=3960",
    "status=verified_contiguous_layout",
    "reason=INVALID_FV_HEADER_OR_BLOCKMAP", "Used=N/A",
)
missing = [s for s in required if s not in output]
if missing:
    raise SystemExit(f"OVMF Shell output lacked {missing}:\n{output}")
print("PASS: UEFI Shell output meets known-good and corrupt FV expectations")
