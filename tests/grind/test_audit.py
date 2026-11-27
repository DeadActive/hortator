"""Audit CLI contract tests; all successful-build fixtures are synthetic."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / 'tools/grind/audit.py'
PIN = '44c9ad4aa57bebf63f412bd552e57143c30b4065'
AREAS = ('sequencer', 'cue', 'recording', 'ui_edits', 'midi_edits', 'editor_edits',
         'project_storage', 'variation_and_lock_storage', 'boot_and_hal')


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
    def run_record(self, record, *options):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'audit.json'
            path.write_text(json.dumps(record), encoding='utf-8')
            return subprocess.run([sys.executable, str(CLI), *options, str(path)],
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

    def test_t03_gate_requires_reviewed_callsite_coverage(self):
        a = fixture()
        a.update(resource_status='pass', firmware_tree='a' * 40,
                 reviewed_firmware_tree=None, integration_points=[])
        result = self.run_record(a, '--complete')
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('call-site coverage', result.stdout)

    def test_complete_t03_passes_only_with_current_review_and_builds(self):
        a = fixture()
        point = a['integration_points'][0]
        a.update(resource_status='pass', firmware_tree='a' * 40,
                 reviewed_firmware_tree='a' * 40,
                 integration_points=[dict(point, area=area, symbol=f'fixture_{area}') for area in AREAS])
        result = self.run_record(a, '--complete')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS: complete T03 audit', result.stdout)
        for update, code in (({'reviewed_firmware_tree': 'b' * 40}, 2),
                             ({'unknowns': ['unverified evidence']}, 2),
                             ({'host_build': 'blocked'}, 2),
                             ({'resource_status': 'fail'}, 1),
                             ({'var_cap': None}, 1)):
            with self.subTest(update=update):
                self.assertEqual(self.run_record(dict(a, **update), '--complete').returncode, code)
        a['integration_points'][-1]['symbol'] = a['integration_points'][0]['symbol']
        self.assertEqual(self.run_record(a, '--complete').returncode, 1)

    def test_builder_rejects_stale_native_revision(self):
        result = subprocess.run(
            [sys.executable, str(CLI.with_name('build_audit.py'))], cwd=ROOT,
            env=dict(os.environ, T03_NATIVE=json.dumps({'revision': '0' * 40})),
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('native evidence revision does not match', result.stderr)

    def test_ram_and_flash_require_ten_percent_reserve(self):
        for budget in ('ram_app', 'flash_app'):
            for used, expected_exit in ((90, 0), (91, 1), (100, 1)):
                with self.subTest(budget=budget, used=used):
                    a = fixture()
                    a[f'{budget}_used'] = used
                    result = self.run_record(a)
                    self.assertEqual(result.returncode, expected_exit, result)
                    if expected_exit:
                        self.assertIn(f'{budget}_used', result.stderr)
                        self.assertIn('10% reserve', result.stderr)

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
