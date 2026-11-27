"""Exercise the resource report CLI with synthetic build artifacts."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "tools/grind/resource_report.py"


class ResourceReportTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.build = Path(scratch.name)
        self.log = self.build / "native-build.log"

    def run_report(self, *options):
        result = subprocess.run(
            [sys.executable, str(CLI), "--build-dir", str(self.build),
             "--build-log", str(self.log), *options],
            check=False, capture_output=True, text=True,
        )
        self.assertTrue(result.stdout, result.stderr)
        return result.returncode, json.loads(result.stdout)

    def write_memory_fixture(self, *, image_size=2048, logged_size=2048,
                             ram=80_000, pool=320_000):
        (self.build / "felucca.bin").write_bytes(bytes(image_size))
        self.log.write_text(
            f"  ok    image {logged_size} B; RAM .data+.bss {ram} B of 98304; "
            f"pool {pool} B of 344064\n",
            encoding="utf-8",
        )

    def test_missing_build_artifacts_are_blocked(self):
        exit_code, report = self.run_report()
        self.assertEqual(exit_code, 2)
        self.assertEqual(report["status"], "BLOCKED")
        self.assertEqual(report["checks"]["flash_app"]["status"], "BLOCKED")
        self.assertIsNone(report["checks"]["flash_app"]["used_bytes"])
        self.assertEqual(report["checks"]["audio_deadline"]["status"], "BLOCKED")
        self.assertEqual(report["checks"]["audio_deadline"]["max_percent"], 80)
        self.assertEqual(report["checks"]["stack_main_high_water"]["status"], "BLOCKED")
        self.assertEqual(report["checks"]["stack_interrupt_high_water"]["status"], "BLOCKED")

    def test_build_summary_is_cross_checked_with_image(self):
        self.write_memory_fixture(logged_size=4096)
        exit_code, report = self.run_report()
        self.assertEqual(exit_code, 1)
        self.assertEqual(report["checks"]["memory_evidence"]["status"], "FAIL")
        self.assertEqual(report["checks"]["ram_app"]["status"], "BLOCKED")

    def test_memory_budgets_use_unrounded_reserve_and_upstream_pool_floor(self):
        self.write_memory_fixture(ram=88_473, pool=335_872)
        exit_code, report = self.run_report()
        self.assertEqual(exit_code, 2)  # ELF and listings are still unavailable.
        self.assertEqual(report["checks"]["ram_app"]["status"], "PASS")
        self.assertEqual(report["checks"]["pool"]["status"], "PASS")
        self.assertEqual(report["checks"]["pool"]["free_bytes"], 8192)
        self.assertEqual(report["checks"]["flash_app"]["used_bytes"], 2048)
        self.assertEqual(report["checks"]["flash_app"]["limit_bytes"], 0x8DFBC)
        self.assertIn("sha256", report["artifacts"]["app_image"])

        self.write_memory_fixture(ram=88_474, pool=335_873)
        exit_code, report = self.run_report()
        self.assertEqual(exit_code, 1)
        self.assertEqual(report["checks"]["ram_app"]["status"], "FAIL")
        self.assertEqual(report["checks"]["pool"]["status"], "FAIL")

    def test_stack_listing_separates_main_and_nested_interrupts(self):
        self.write_memory_fixture()
        listing = "\n".join([
            "fm1_cstart:", " 2000120: 00 00 \tsp += -100",
            "isr_timer5:", " 2001120: 00 00 \tsp += -200",
            "isr_alnk0:", " 2002120: 00 00 \tsp += -300",
        ])
        (self.build / "felucca.dis").write_text(listing, encoding="utf-8")
        (self.build / "text.bin").write_bytes(b"Synthetic text")
        (self.build / "data.bin").write_bytes(b"Synthetic data")
        (self.build / "ramtext.bin").write_bytes(b"Synthetic RAM text")
        _, report = self.run_report()
        self.assertEqual(report["checks"]["stack_main_static"]["status"], "PASS")
        self.assertEqual(report["checks"]["stack_main_static"]["used_bytes"], 100)
        self.assertEqual(report["checks"]["stack_interrupt_static"]["status"], "PASS")
        self.assertEqual(report["checks"]["stack_interrupt_static"]["used_bytes"], 500)

        (self.build / "data.bin").unlink()
        _, report = self.run_report()
        self.assertEqual(report["checks"]["stack_main_static"]["status"], "BLOCKED")

    def test_empty_stack_entry_does_not_pass(self):
        (self.build / "felucca.dis").write_text(
            "fm1_cstart:\nisr_timer5:\nisr_alnk0:\n", encoding="utf-8"
        )
        (self.build / "text.bin").write_bytes(b"Synthetic text")
        (self.build / "data.bin").write_bytes(b"Synthetic data")
        (self.build / "ramtext.bin").write_bytes(b"Synthetic RAM text")
        _, report = self.run_report()
        self.assertEqual(report["checks"]["stack_main_static"]["status"], "FAIL")
        self.assertEqual(report["checks"]["stack_interrupt_static"]["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
