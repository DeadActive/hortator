# SPDX-License-Identifier: GPL-3.0-only
"""Exercise CI status propagation with small executable lane fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WRAPPER = Path('tools/grind/run_ci.sh')


class CiPolicyTests(unittest.TestCase):
    def run_lane(self, lane, script):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / WRAPPER.parent).mkdir(parents=True)
            (root / 'tests').mkdir()
            shutil.copyfile(ROOT / WRAPPER, root / WRAPPER)
            (root / 'tests/run_drum_tests.sh').write_text(script, encoding='utf-8')
            output = root / 'status'
            result = subprocess.run(['bash', str(WRAPPER), lane], cwd=root,
                                    env=dict(os.environ, GITHUB_OUTPUT=str(output)),
                                    capture_output=True, text=True)
            return result, output.read_text(encoding='utf-8').strip()

    def test_unavailable_instruction_measurement_is_skipped(self):
        result, status = self.run_lane(
            'performance', "test \"$GRIND_REQUIRE_PERFORMANCE\" = 0 || exit 1\n"
            "echo 'SKIPPED: performance: host instruction counter unavailable'\n"
            "echo 'PASS: functional suite'\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(status, 'status=SKIPPED')
        self.assertIn('PASS: functional suite', result.stdout)

    def test_lost_instruction_counter_is_skipped(self):
        result, status = self.run_lane(
            'performance', "echo 'BLOCKED: performance: host instruction counter stopped responding'\nexit 2\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(status, 'status=SKIPPED')

    def test_lost_instruction_counter_does_not_hide_earlier_failure(self):
        result, status = self.run_lane(
            'performance', "echo 'FAIL  earlier functional check'\n"
            "echo 'BLOCKED: performance: host instruction counter stopped responding'\nexit 2\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(status, 'status=FAIL')

    def test_historical_skip_preserves_functional_pass(self):
        result, status = self.run_lane(
            'host-upstream', "echo 'SKIPPED: historical: frozen reference unavailable'\necho 'PASS: functional suite'\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(status, 'status=PASS')
        self.assertIn('SKIPPED: historical:', result.stdout)

    def test_instruction_regression_still_fails_the_performance_lane(self):
        result, status = self.run_lane(
            'performance', "echo 'FAIL: performance: instruction budget exceeded'\nexit 1\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(status, 'status=FAIL')

    def test_real_failure_is_not_hidden_by_optional_skip(self):
        result, status = self.run_lane(
            'host-upstream', "echo 'SKIPPED: historical: frozen reference unavailable'\necho 'FAIL: functional check'\nexit 1\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(status, 'status=FAIL')

    def test_required_skip_still_blocks(self):
        result, status = self.run_lane('host-upstream', "echo 'fidelity: SKIPPED (offline)'\n")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertEqual(status, 'status=BLOCKED')


if __name__ == '__main__':
    unittest.main()
