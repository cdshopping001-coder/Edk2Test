#!/usr/bin/env python3
"""Create deterministic FV inputs for real OVMF Shell test."""
from pathlib import Path
import sys
from test_raw_fv_fixtures import make_fixture
from test_fbpt_fixtures import make_fbpt
from test_fpdt_acpi_fixtures import make_fpdt
out = Path(sys.argv[1])
raw, used = make_fixture(inverted=True)
bad, _ = make_fixture(inverted=True, bad_checksum=True)
assert len(raw) == 4096 and used == 136
(out / "VALID.FV").write_bytes(raw)
(out / "BAD.FV").write_bytes(bad)
# Two nonoverlapping standard volumes surrounded by opaque ROM bytes.
dual = bytes([0x5A]) * 4096 + raw + bytes([0xFF]) * 4096 + raw + bytes([0x00]) * 1024
(out / "DUAL.ROM").write_bytes(dual)

(out / 'VALID.FBPT').write_bytes(make_fbpt())
(out / 'BAD.FBPT').write_bytes(make_fbpt(bad_record_length=True))
(out / 'SHELL.FBPT').write_bytes(make_fbpt(os_missing=True))

(out / 'VALID.FPDT').write_bytes(make_fpdt())
(out / 'BAD.FPDT').write_bytes(make_fpdt(bad_checksum=True))
