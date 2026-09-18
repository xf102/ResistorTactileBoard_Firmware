import unittest
import zlib


class FrameProtocolReferenceTests(unittest.TestCase):
    """Lock the host-visible wire format to hand-checked byte fixtures."""

    def test_two_by_two_frame_reference_vector(self):
        counter_and_samples = bytes.fromhex(
            "00 2A 12 34 AB CD 00 01 FF FF"
        )
        expected = bytes.fromhex(
            "FF 66 00 2A 12 34 AB CD 00 01 FF FF D3 62 5D 4B"
        )

        crc = zlib.crc32(counter_and_samples).to_bytes(4, "little")
        actual = bytes.fromhex("FF 66") + counter_and_samples + crc

        self.assertEqual(actual, expected)
        self.assertEqual(len(actual), 2 + 2 + 4 * 2 + 4)

    def test_counter_wrap_is_encoded_big_endian(self):
        counter = 0xFFFF
        self.assertEqual(counter.to_bytes(2, "big"), bytes.fromhex("FF FF"))
        self.assertEqual(((counter + 1) & 0xFFFF).to_bytes(2, "big"), b"\x00\x00")


if __name__ == "__main__":
    unittest.main()
