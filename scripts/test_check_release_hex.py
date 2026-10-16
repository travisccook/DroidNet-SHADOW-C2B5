"""Tests for check_release_hex.py: run with python3 -m unittest scripts/test_check_release_hex.py"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_release_hex as crh  # noqa: E402

MAC = "00:1A:2B:3C:4D:05"
MAC_BYTES = bytes.fromhex("001A2B3C4D05")


def ihex(data, base=0):
    """Intel HEX for `data` at `base`, 16 bytes a record, with a type-04 record for the upper 16 bits."""
    lines = []

    def rec(kind, addr, payload):
        raw = bytes([len(payload), (addr >> 8) & 0xFF, addr & 0xFF, kind]) + payload
        return ":" + (raw + bytes([(-sum(raw)) & 0xFF])).hex().upper()

    lines.append(rec(4, 0, bytes([(base >> 24) & 0xFF, (base >> 16) & 0xFF])))
    for off in range(0, len(data), 16):
        lines.append(rec(0, (base + off) & 0xFFFF, data[off:off + 16]))
    lines.append(rec(1, 0, b""))
    return "\n".join(lines) + "\n"


class CheckTest(unittest.TestCase):
    def write(self, text):
        f = tempfile.NamedTemporaryFile("w", suffix=".hex", delete=False)
        f.write(text)
        f.close()
        self.addCleanup(os.unlink, f.name)
        return f.name

    def test_clean_image_passes(self):
        path = self.write(ihex(b"\x00\x01SHADOW firmware\xff" * 8))
        self.assertEqual(crh.main([path, "--mac", MAC]), 0)

    def test_finds_text_and_bytes(self):
        for blob in (MAC.encode(), MAC.lower().encode(), MAC_BYTES, MAC_BYTES[::-1]):
            path = self.write(ihex(b"\x00" * 20 + blob + b"\x00" * 20, base=0x10000))
            self.assertEqual(crh.main([path, "--mac", MAC]), 1, blob)

    def test_reads_addresses_from_the_header(self):
        header = self.write(
            '#define PS3_FOOT_MAC "00:1A:2B:3C:4D:05"\n#define PS3_DOME_MAC "XX:XX:XX:XX:XX:XX"\n'
            '#define PS3_BACKUP_FOOT_MAC "00:00:00:00:00:00"\n')
        self.assertEqual(crh.macs_in_header(header), [MAC])

    def test_bad_checksum_is_bad_input(self):
        lines = ihex(b"abcd").splitlines()
        data = lines[1]  # the data record (line 0 is the type-04 record)
        lines[1] = data[:-2] + ("00" if data[-2:] != "00" else "01")
        path = self.write("\n".join(lines) + "\n")
        self.assertEqual(crh.main([path, "--mac", MAC]), 2)


if __name__ == "__main__":
    unittest.main()
