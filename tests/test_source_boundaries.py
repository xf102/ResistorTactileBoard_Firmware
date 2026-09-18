import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "Debug"
COMPILE_DB = BUILD_DIR / "compile_commands.json"


class FirmwareBuildBoundaryTests(unittest.TestCase):
    """Verify the actual firmware target is standalone and contains its core BSP."""

    def test_firmware_compiles_required_root_bsp_sources_only(self):
        self.assertTrue(COMPILE_DB.exists(), "configure the Debug preset first")
        commands = json.loads(COMPILE_DB.read_text(encoding="utf-8"))
        compiled_files = {
            Path(entry["file"]).resolve().relative_to(ROOT).as_posix()
            for entry in commands
            if Path(entry["file"]).resolve().is_relative_to(ROOT)
        }

        required = {
            "app/frame_protocol.c",
            "app/frame_service.c",
            "app/adc_frontend_ads8681.c",
            "app/app_events.c",
            "app/scan_service.c",
            "app/dataport.c",
            "Drivers/BSP/74HC595/drv_74hc595.c",
            "Drivers/BSP/ADS8681/drv_ads8681.c",
            "Drivers/BSP/CD74HC4067/drv_cd74hc4067.c",
            "Drivers/BSP/Product/product.c",
            "Drivers/BSP/bsp_init.c",
        }
        self.assertTrue(required.issubset(compiled_files), required - compiled_files)

        compile_text = COMPILE_DB.read_text(encoding="utf-8").replace("\\", "/")
        self.assertNotIn("ResistorTactileBoard-1", compile_text)
        self.assertNotIn("rt-thread-nano", compile_text)


if __name__ == "__main__":
    unittest.main()
