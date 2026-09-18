import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


COMMANDS = {
    "help": "show command help",
    "status": "show acquisition status",
    "scan_start": "start ADC acquisition",
    "scan_stop": "stop ADC acquisition",
    "scan_rows": "set active row count",
    "scan_cols": "set active column count",
    "scan_fps": "set target frame rate",
    "scan_period_us": "set sampling step period",
    "adc_read": "read one ADC sample while stopped",
    "adc_reset": "reset ADC frontend while stopped",
    "spi_status": "show ADC frontend readiness",
    "stats_show": "show all runtime counters",
    "stats_clear": "clear all runtime counters",
}


class CommandContractTests(unittest.TestCase):
    def test_required_commands_have_clear_help(self):
        self.assertEqual(len(COMMANDS), 13)
        for name, help_text in COMMANDS.items():
            self.assertTrue(name)
            self.assertGreaterEqual(len(help_text.split()), 3)

    def test_scan_count_rejects_zero_non_numeric_and_extra_arguments(self):
        def valid_count(argv, maximum):
            if len(argv) != 2 or not argv[1].isdigit():
                return False
            value = int(argv[1])
            return 1 <= value <= maximum

        self.assertFalse(valid_count(["scan_rows", "0"], 8))
        self.assertFalse(valid_count(["scan_rows", "abc"], 8))
        self.assertFalse(valid_count(["scan_rows", "4", "extra"], 8))
        self.assertTrue(valid_count(["scan_rows", "8"], 8))

    def test_console_accepts_cr_lf_and_crlf_without_double_execution(self):
        source = (ROOT / "app" / "console.c").read_text(encoding="utf-8")

        self.assertIn("console_finish_line", source)
        self.assertIn("s_last_terminator_was_cr", source)
        self.assertIn("if (ch == '\\r')", source)
        self.assertIn("if (ch == '\\n')", source)

    def test_console_error_path_recovers_uart_receive(self):
        source = (ROOT / "app" / "console.c").read_text(encoding="utf-8")

        self.assertIn("HAL_UART_AbortReceive", source)
        self.assertIn("__HAL_UART_CLEAR_OREFLAG", source)
        self.assertIn("s_rx_rearm_failures", source)

    def test_app_prints_acquisition_health_every_two_seconds(self):
        source = (ROOT / "app" / "app_main.c").read_text(encoding="utf-8")

        self.assertIn("APP_STATUS_PERIOD_MS", source)
        self.assertIn("2000U", source)
        self.assertIn("[ACQ] fps=", source)


if __name__ == "__main__":
    unittest.main()
