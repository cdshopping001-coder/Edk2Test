"""Generate raw PI firmware volumes with sections and true EFI compression."""
import struct
import subprocess
import tempfile
import uuid
from pathlib import Path
from test_raw_fv_fixtures import make_fixture, file_record, FS2

FV_BYTES = 16384
HEADER_BYTES = 72

def raw_fv_with_section(section: bytes) -> tuple[bytes, int]:
    data = bytearray([0xFF] * FV_BYTES)
    hdr = bytearray(struct.pack(
        "<16s16sQIIHHHBB",
        b"\0" * 16, FS2.bytes_le, FV_BYTES, 0x4856465F,
        0x800, HEADER_BYTES, 0, 0, 0, 2,
    ) + struct.pack("<IIII", 1, FV_BYTES, 0, 0))
    struct.pack_into("<H", hdr, 50,
                     (-sum(struct.unpack("<36H", hdr))) & 0xFFFF)
    assert sum(struct.unpack("<36H", hdr)) % 65536 == 0
    ffs = file_record(0x0B, section, True)
    used = HEADER_BYTES + len(ffs)
    data[:HEADER_BYTES] = hdr
    data[HEADER_BYTES:used] = ffs
    return bytes(data), used

def section_bytes(kind: int, content: bytes) -> bytes:
    size = len(content) + 4
    assert size < 0xFFFFFF
    return size.to_bytes(3, "little") + bytes([kind]) + content

def make_nested_plain_fv() -> tuple[bytes, int]:
    child, child_used = make_fixture(inverted=True)
    assert child_used == 136
    return raw_fv_with_section(section_bytes(0x17, child))

def find_efi_compressor(root: Path) -> Path:
    paths = (
        root / "edk2/BaseTools/Source/C/bin/TianoCompress",
        root / "edk2/BaseTools/Source/C/bin/TianoCompress.exe",
        root / "edk2/BaseTools/Bin/Linux-x86_64/TianoCompress",
    )
    for candidate in paths:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("EDK2 TianoCompress (supports --uefi) was not built")

def make_nested_efi_compressed_fv(root: Path) -> tuple[bytes, int]:
    child, _ = make_fixture(inverted=True)
    plain = section_bytes(0x17, child)
    with tempfile.TemporaryDirectory() as t:
        inp = Path(t) / "uncompressed.bin"
        enc = Path(t) / "compressed.bin"
        inp.write_bytes(plain)
        subprocess.run([
            str(find_efi_compressor(root)), "--uefi", "-e",
            "-o", str(enc), str(inp),
        ], check=True, capture_output=True)
        compressed = enc.read_bytes()
    assert compressed and len(compressed) < 8 * 1024 * 1024
    # EFI_COMPRESSION_SECTION: 4-byte section header, UINT32 uncompressed size,
    # UINT8 algorithm (1=standard EFI compression), followed by compressed bytes.
    payload = struct.pack("<IB", len(plain), 1) + compressed
    return raw_fv_with_section(section_bytes(0x01, payload))
