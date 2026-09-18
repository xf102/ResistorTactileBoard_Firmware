import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EIDE_CONFIG = ROOT / ".eide" / "eide.yml"


class EideBuildConfigTests(unittest.TestCase):
    """Keep the EIDE target aligned with the standalone firmware sources."""

    def test_eide_build_includes_application_and_bsp_modules(self):
        config = EIDE_CONFIG.read_text(encoding="utf-8")

        self.assertIn("  - app\n", config)
        self.assertNotIn("    excludeList:\n      - Drivers/BSP\n", config)

        legacy_sources = {
            "      - Drivers/BSP/BasicService",
            "      - Drivers/BSP/Status_LED",
            "      - Drivers/BSP/TPC5120S16",
            "      - app/data_acquisition_app.c",
            "      - app/data_acquisition_debug.c",
            "      - app/tpc5120_matrix_diag.c",
            "      - app/usart2_dataport_app.c",
        }
        for entry in legacy_sources:
            self.assertEqual(
                2,
                config.count(entry + "\n"),
                f"Debug and Release must both exclude: {entry.strip()}",
            )

        required_include_entries = {
            "        - app",
            "        - Drivers/BSP",
            "        - Drivers/BSP/74HC595",
            "        - Drivers/BSP/ADS8681",
            "        - Drivers/BSP/CD74HC4067",
            "        - Drivers/BSP/Product",
        }
        for entry in required_include_entries:
            self.assertEqual(
                2,
                config.count(entry + "\n"),
                f"Debug and Release must both contain: {entry.strip()}",
            )


if __name__ == "__main__":
    unittest.main()
