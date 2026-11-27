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

    def write_memory_fixture(self, *, image_size=2048, logged_size=None,
                             ram=80_000, pool=320_000):
        (self.build / "felucca.bin").write_bytes(bytes(image_size))
        if logged_size is None:
            logged_size = image_size
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
        self.assertIsNone(report["checks"]["audio_deadline"]["used_percent"])
        self.assertNotIn("max_percent", report["checks"]["audio_deadline"])
        self.assertEqual(report["checks"]["stack_main_high_water"]["status"], "BLOCKED")
        self.assertEqual(report["checks"]["stack_interrupt_high_water"]["status"], "BLOCKED")
        self.assertEqual(report["checks"]["upstream_comparison"]["status"], "SKIPPED")

    def test_build_summary_is_cross_checked_with_image(self):
        self.write_memory_fixture(logged_size=4096)
        exit_code, report = self.run_report()
        self.assertEqual(exit_code, 1)
        self.assertEqual(report["checks"]["memory_evidence"]["status"], "FAIL")
        self.assertEqual(report["checks"]["ram_app"]["status"], "BLOCKED")

    def test_memory_budgets_use_actual_regions_and_upstream_pool_floor(self):
        at_limits = {"image_size": 0x8DFBC, "ram": 98_304, "pool": 335_872}
        self.write_memory_fixture(**at_limits)
        exit_code, report = self.run_report()
        self.assertEqual(exit_code, 2)  # Required native checks still lack their artifacts.
        for check, free in (("ram_app", 0), ("flash_app", 0), ("pool", 8192)):
            self.assertEqual(report["checks"][check]["status"], "PASS")
            self.assertEqual(report["checks"][check]["free_bytes"], free)
            self.assertEqual(report["checks"][check]["min_free_bytes"], free)
        self.assertIn("sha256", report["artifacts"]["app_image"])

        for field, check in (("image_size", "flash_app"), ("ram", "ram_app"), ("pool", "pool")):
            with self.subTest(check=check):
                self.write_memory_fixture(**dict(at_limits, **{field: at_limits[field] + 1}))
                exit_code, report = self.run_report()
                self.assertEqual(exit_code, 1)
                self.assertEqual(report["checks"][check]["status"], "FAIL")

    def test_explicit_incomplete_historical_build_is_blocked(self):
        reference = self.build / "historical"
        reference.mkdir()
        (reference / "felucca.dis").write_text("incomplete comparison input\n", encoding="utf-8")
        _, report = self.run_report("--upstream-build-dir", str(reference))
        self.assertEqual(report["checks"]["upstream_comparison"]["status"], "BLOCKED")

    def test_present_historical_comparison_failure_is_a_failure(self):
        reference = self.build / "historical"
        reference.mkdir()
        for build in (self.build, reference):
            (build / "felucca.dis").write_text("invalid disassembly\n", encoding="utf-8")
            (build / "felucca.elf").write_bytes(b"invalid ELF")
        exit_code, report = self.run_report("--upstream-build-dir", str(reference))
        self.assertEqual(exit_code, 1)
        self.assertEqual(report["checks"]["upstream_comparison"]["status"], "FAIL")

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
        self.assertTrue(report["checks"]["stack_main_static"]["diagnostic_only"])
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
