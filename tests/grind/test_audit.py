"""Audit CLI contract tests; all successful-build fixtures are synthetic."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / 'tools/grind/audit.py'
PIN = '44c9ad4aa57bebf63f412bd552e57143c30b4065'


def fixture():
    return {
        'upstream_sha': PIN, 'native_build': 'pass', 'host_build': 'pass',
        'wasm_build': 'pass', 'step_bytes': 4, 'project_bytes': 3800,
        'var_cap': 3576, 'ram_app_used': 80, 'ram_app_limit': 100,
        'flash_app_used': 80, 'flash_app_limit': 100,
        'integration_points': [{'path': 'firmware/src/seq.c', 'symbol': 'events_block',
                                'owner': 'ISR', 'callers': ['audio ISR']}],
        'unknowns': []
    }


class AuditTests(unittest.TestCase):
    def run_record(self, record):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'audit.json'
            path.write_text(json.dumps(record), encoding='utf-8')
            return subprocess.run([sys.executable, str(CLI), str(path)],
                                  capture_output=True, text=True)

    def test_synthetic_complete_record(self):
        result = self.run_record(fixture())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('AUDIT STRUCTURE PASS', result.stdout)

    def test_native_blocked_with_unknown_measurements(self):
        a = fixture()
        a.update(native_build='blocked', host_build='blocked', wasm_build='blocked',
                 project_bytes=None, ram_app_used=None, ram_app_limit=None,
                 flash_app_used=None, flash_app_limit=None,
                 unknowns=['native checkout unavailable'])
        result = self.run_record(a)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('BLOCKED', result.stdout)

    def test_wrong_pin(self):
        a = fixture(); a['upstream_sha'] = '0' * 40
        result = self.run_record(a)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('upstream_sha', result.stderr)

    def test_missing_field(self):
        a = fixture(); del a['project_bytes']
        result = self.run_record(a)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('project_bytes', result.stderr)

    def test_invalid_sizes(self):
        for key, value in [('step_bytes', 5), ('var_cap', 3584),
                           ('project_bytes', 3841), ('project_bytes', None),
                           ('ram_app_used', 101), ('ram_app_used', True),
                           ('flash_app_limit', 0), ('flash_app_used', -1)]:
            with self.subTest(key=key, value=value):
                a = fixture(); a[key] = value
                result = self.run_record(a)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(key, result.stderr)

    def test_build_status_not_green(self):
        a = fixture(); a['wasm_build'] = 'fail'
        self.assertEqual(self.run_record(a).returncode, 2)

    def test_integration_point_needs_callers(self):
        a = fixture(); a['integration_points'][0]['callers'] = []
        result = self.run_record(a)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('integration_points', result.stderr)

    def test_invalid_record_type(self):
        result = self.run_record([])
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('object', result.stderr)


if __name__ == '__main__':
    unittest.main()
