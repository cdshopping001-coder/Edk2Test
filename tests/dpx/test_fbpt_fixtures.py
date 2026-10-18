"""Deterministic ACPI FPDT/FBPT test records (host side fixture geometry only)."""
import struct
import unittest

FPDT_BASIC_BOOT_TYPE = 0x0002
BASIC_BOOT_REVISION = 0x02


def make_fbpt(*, bad_record_length=False, bad_signature=False, os_missing=False):
    if os_missing:
        times = (500_000_000, 0, 0, 0, 0)
    else:
        times = (
            500_000_000,
            800_000_000,
            850_000_000,
            1_300_000_000,
            1_350_000_000,
        )
    record = struct.pack(
        "<HBBI5Q",
        FPDT_BASIC_BOOT_TYPE,
        48,
        BASIC_BOOT_REVISION,
        0,
        *times,
    )
    assert len(record) == 48
    other = struct.pack("<HBBI", 0x9001, 8, 1, 0x12345678)
    if bad_record_length:
        other = struct.pack("<HBBI", 0x9001, 0, 1, 0x12345678)
    length = 8 + len(record) + len(other)
    header = struct.pack("<4sI", b"FAIL" if bad_signature else b"FBPT", length)
    return header + record + other


class FbptFixtures(unittest.TestCase):
    def test_valid_basic_boot(self):
        raw = make_fbpt()
        signature, length = struct.unpack_from("<4sI", raw)
        self.assertEqual((signature, length), (b"FBPT", 64))
        record_type, record_length, revision = struct.unpack_from("<HBB", raw, 8)
        self.assertEqual((record_type, record_length, revision), (2, 48, 2))
        self.assertEqual(struct.unpack_from("<5Q", raw, 16)[0], 500_000_000)

    def test_bad_record_length(self):
        raw = make_fbpt(bad_record_length=True)
        self.assertEqual(struct.unpack_from("<B", raw, 8 + 48 + 2)[0], 0)

    def test_missing_os_events(self):
        raw = make_fbpt(os_missing=True)
        self.assertEqual(struct.unpack_from("<5Q", raw, 16),
                         (500_000_000, 0, 0, 0, 0))

    def test_bad_signature(self):
        self.assertEqual(make_fbpt(bad_signature=True)[:4], b"FAIL")


if __name__ == "__main__":
    unittest.main()
