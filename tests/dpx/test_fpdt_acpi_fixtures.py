"""Synthetic ACPI FPDT pointer record checks, not a firmware-memory access test."""
import struct
import unittest


def make_fpdt(*, bad_checksum=False, zero_record_length=False):
    boot = struct.pack("<HBBIQ", 0, 16, 1, 0, 0x12345000)
    s3 = struct.pack("<HBBIQ", 1, 16, 1, 0, 0)
    unknown = struct.pack("<HBBI", 0x9001, 0 if zero_record_length else 8, 1, 0)
    body = boot + s3 + unknown
    length = 36 + len(body)
    raw = bytearray(struct.pack(
        "<4sIBB6s8sI4sI",
        b"FPDT", length, 1, 0, b"DPXCI ", b"DPXTEST ", 1,
        b"DPX ", 1,
    ) + body)
    assert len(raw) == length == 76
    raw[9] = (-sum(raw)) & 0xFF
    assert sum(raw) % 256 == 0
    if bad_checksum:
        raw[9] ^= 1
    return bytes(raw)


class AcpiFpdtFixtures(unittest.TestCase):
    def test_valid_header_checksum_pointer(self):
        fpdt = make_fpdt()
        self.assertEqual(fpdt[:4], b"FPDT")
        self.assertEqual(struct.unpack_from("<I", fpdt, 4)[0], 76)
        self.assertEqual(sum(fpdt) % 256, 0)
        self.assertEqual(struct.unpack_from("<Q", fpdt, 36+8)[0], 0x12345000)

    def test_checksum_corruption(self):
        self.assertNotEqual(sum(make_fpdt(bad_checksum=True)) % 256, 0)

    def test_zero_record_length(self):
        fpdt = make_fpdt(zero_record_length=True)
        self.assertEqual(fpdt[36 + 16 + 16 + 2], 0)


if __name__ == "__main__":
    unittest.main()
