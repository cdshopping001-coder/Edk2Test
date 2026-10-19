"""FPDT callback Start/End sample with a real OVMF FFS GUID."""
import struct
import unittest
import uuid
from test_fbpt_fixtures import make_fbpt

PCI_BUS_DXE = uuid.UUID("93b80004-9fb3-11d4-9a3a-0090273fc14d")
EVENT_GUID = uuid.UUID("aaaaaaa0-0000-0000-0000-000000000001")
TOKEN = b"PciEnumerator\x00"


def event(progress, time_ns):
    # EDK2 FPDT_DUAL_GUID_STRING_EVENT_RECORD: type, length, rev,
    # ProgressId, ApicId, timestamp, Guid1(module), Guid2(trigger), String.
    size = 50 + len(TOKEN)
    raw = struct.pack(
        "<HBBHIQ16s16s", 0x1012, size, 1, progress, 0, time_ns,
        PCI_BUS_DXE.bytes_le, EVENT_GUID.bytes_le,
    ) + TOKEN
    assert len(raw) == size
    return raw


def make_event_fbpt():
    base = make_fbpt()
    extra = (
        event(0x20, 1_000_000_000) +
        event(0x21, 1_120_000_000)
    )
    # Existing valid FBPT has BasicBoot + 8-byte unknown vendor record.
    return b"FBPT" + struct.pack("<I", len(base) + len(extra)) + base[8:] + extra


class EventFixtures(unittest.TestCase):
    def test_dual_guid_callback_pair(self):
        buf = make_event_fbpt()
        self.assertEqual(struct.unpack_from("<I", buf, 4)[0], len(buf))
        self.assertEqual(buf.count(PCI_BUS_DXE.bytes_le), 2)
        self.assertEqual(buf.count(EVENT_GUID.bytes_le), 2)
        self.assertEqual(buf.count(TOKEN), 2)


if __name__ == "__main__":
    unittest.main()
