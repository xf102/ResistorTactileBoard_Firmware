import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def map_sample(raw, baseline, deadband=50):
    threshold = min(65535, baseline + deadband)
    if raw <= threshold or threshold >= 65535:
        return 0
    return min(1019, 1 + ((raw - threshold) * 1018) // (65535 - threshold))


class SampleMapperContractTests(unittest.TestCase):
    def test_mapping_boundaries_and_saturation(self):
        self.assertEqual(0, map_sample(5000, 5000))
        self.assertEqual(0, map_sample(5050, 5000))
        self.assertEqual(1, map_sample(5051, 5000))
        self.assertEqual(1019, map_sample(65535, 5000))
        self.assertEqual(0, map_sample(65535, 65535))

    def test_firmware_has_per_point_50_frame_calibrator(self):
        header = ROOT / "app" / "sample_mapper.h"
        source = ROOT / "app" / "sample_mapper.c"

        self.assertTrue(header.exists())
        self.assertTrue(source.exists())
        text = source.read_text(encoding="utf-8")
        self.assertIn("SAMPLE_MAPPER_CALIBRATION_FRAMES", text)
        self.assertIn("50U", text)
        self.assertIn("SAMPLE_MAPPER_DEADBAND_RAW", text)
        self.assertIn("1019U", text)

    def test_mapper_runs_before_frame_protocol_pack(self):
        source = (ROOT / "app" / "dataport.c").read_text(encoding="utf-8")
        mapper = source.index("sample_mapper_process_frame")
        packer = source.index("frame_protocol_pack_inplace")

        self.assertLess(mapper, packer)

    def test_zero_calibrate_command_is_available(self):
        source = (ROOT / "app" / "command.c").read_text(encoding="utf-8")

        self.assertIn('"zero_calibrate"', source)
        self.assertIn("sample_mapper_restart_calibration", source)


if __name__ == "__main__":
    unittest.main()
