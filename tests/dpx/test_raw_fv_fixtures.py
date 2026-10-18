"""Golden PI FV fixtures for future DpX Shell runtime tests."""
import struct
import unittest
import uuid

TOTAL = 4096
HEADER_LENGTH = 72
FS2 = uuid.UUID("8c8ce578-8a3d-4f1c-9935-896185c32dd3")
FFS = uuid.UUID("11111111-2222-3333-4444-555555555555")

def file_record(file_type, body, inverted, large=False):
    size = (32 if large else 24) + len(body)
    size_field = b"\x00" * 3 if large else size.to_bytes(3, "little")
    raw_state = 0xFB if inverted else 0x04
    entry = bytearray(FFS.bytes_le + bytes([0, 0xAA, file_type, int(large)])
                      + size_field + bytes([raw_state]))
    if large:
        entry += struct.pack("<Q", size)
    entry[16] = (-sum(entry) + entry[17] + entry[23]) & 0xFF
    assert (sum(entry) - entry[17] - entry[23]) % 256 == 0
    occupied = (size + 7) & ~7
    return bytes(entry) + body + bytes([0xFF if inverted else 0]) * (occupied - size)

def make_fixture(inverted=True, large=False, bad_checksum=False, bad_tail=False):
    erase = 0xFF if inverted else 0
    first = file_record(0x07, b"DELLDPX0", inverted, large)
    pad = file_record(0xF0, b"PADTEST0", inverted)
    used = HEADER_LENGTH + len(first) + len(pad)
    volume = bytearray([erase] * TOTAL)
    hdr = bytearray(struct.pack(
        "<16s16sQIIHHHBB", b"\x00" * 16, FS2.bytes_le, TOTAL,
        0x4856465F, 0x800 if inverted else 0, HEADER_LENGTH, 0, 0, 0, 2
    ) + struct.pack("<IIII", 1, TOTAL, 0, 0))
    assert len(hdr) == HEADER_LENGTH
    words = sum(struct.unpack("<36H", hdr))
    struct.pack_into("<H", hdr, 50, (-words) & 0xFFFF)
    assert sum(struct.unpack("<36H", hdr)) % 65536 == 0
    if bad_checksum:
        hdr[50] ^= 1
    volume[:HEADER_LENGTH] = hdr
    volume[HEADER_LENGTH:used] = first + pad
    if bad_tail:
        volume[-1] ^= 1
    return bytes(volume), used

class RawFvFixtures(unittest.TestCase):
    def test_erase_polarities(self):
        for inverted in (True, False):
            raw, used = make_fixture(inverted)
            self.assertEqual(len(raw), TOTAL)
            self.assertEqual(used, 136)
            self.assertEqual(set(raw[used:]), {0xFF if inverted else 0})

    def test_large_ffs_header(self):
        raw, used = make_fixture(True, large=True)
        self.assertEqual(used, 144)
        self.assertEqual(raw[HEADER_LENGTH + 19], 1)
        self.assertEqual(struct.unpack_from("<Q", raw, HEADER_LENGTH + 24)[0], 40)

    def test_bad_checksum(self):
        raw, _ = make_fixture(bad_checksum=True)
        self.assertNotEqual(sum(struct.unpack("<36H", raw[:72])) % 65536, 0)

    def test_non_erased_tail(self):
        raw, used = make_fixture(bad_tail=True)
        self.assertNotEqual(raw[-1], 0xFF)
        self.assertEqual(raw[used], 0xFF)

if __name__ == "__main__":
    unittest.main()
