# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the license check against real files and incomplete evidence."""
from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'assets/grind/manifest.json'
CLI = ROOT / 'tools/grind/check_assets.py'


class CheckAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))

    def check_manifest(self, manifest, sdk=None, source_only=False):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'manifest.json'
            path.write_text(json.dumps(manifest), encoding='utf-8')
            args = [sys.executable, str(CLI), '--manifest', str(path)]
            if sdk is not None:
                args.extend(('--sdk-root', str(sdk)))
            if source_only:
                args.append('--source-only')
            env = os.environ.copy()
            env.pop('AC79_SDK', None)
            return subprocess.run(args, env=env, capture_output=True, text=True)

    def test_current_manifest_verifies_sources_but_remains_blocked(self):
        result = self.check_manifest(self.manifest)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('BLOCKED: asset license gate', result.stdout)
        self.assertIn('docs/panel.jpg: restricted', result.stdout)
        self.assertIn('build/sim/source.tar.gz: restricted', result.stdout)
        self.assertNotIn('0 file digests verified', result.stdout)

    def test_approval_cannot_bypass_open_items(self):
        manifest = deepcopy(self.manifest)
        manifest.update(release_approved=True, open_reviews=[])
        result = self.check_manifest(manifest)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('build/gen/felucca_samples.h: restricted', result.stdout)

    def test_source_gate_can_run_in_a_clean_checkout(self):
        result = self.check_manifest(self.manifest, source_only=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('docs/panel.jpg: restricted', result.stdout)
        manifest = deepcopy(self.manifest)
        for asset in manifest['assets']:
            if asset['kind'] == 'tracked':
                asset.update(status='cleared', license='MIT')
        result = self.check_manifest(manifest, source_only=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS: source asset license gate', result.stdout)
        self.assertEqual(self.check_manifest(manifest).returncode, 2)

    def test_modified_digest_fails(self):
        manifest = deepcopy(self.manifest)
        panel = next(item for item in manifest['assets'] if item['path'] == 'docs/panel.jpg')
        panel['sha256'] = '0' * 64
        result = self.check_manifest(manifest)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn('docs/panel.jpg: SHA-256 mismatch', result.stderr)

    def test_omitting_required_assets_fails(self):
        for kind, target in (
            ('tracked', 'docs/panel.jpg'),
            ('generated', 'build/sim/source.tar.gz'),
            ('sdk', 'cpu/wl82/tools/uboot.boot'),
        ):
            with self.subTest(kind=kind):
                manifest = deepcopy(self.manifest)
                manifest['assets'] = [asset for asset in manifest['assets']
                                      if asset['path'] != target]
                result = self.check_manifest(manifest)
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn(f'missing {kind} inventory entries: {target}', result.stderr)

    def test_generated_bytes_missing_despite_approval(self):
        manifest = deepcopy(self.manifest)
        font = next(item for item in manifest['assets']
                    if item['path'] == 'build/sim/terminus.woff')
        font['status'] = 'cleared'
        result = self.check_manifest(manifest)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('build/sim/terminus.woff: matching artifact evidence is missing',
                      result.stdout)

    def test_mismatched_sdk_bytes_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            sdk = Path(tmp)
            component = sdk / 'cpu/wl82/tools/uboot.boot'
            component.parent.mkdir(parents=True)
            component.write_bytes(b'invalid SDK file')
            result = self.check_manifest(self.manifest, sdk)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn('cpu/wl82/tools/uboot.boot: SHA-256 mismatch', result.stderr)

    def test_traversal_is_rejected(self):
        manifest = deepcopy(self.manifest)
        manifest['assets'][0]['path'] = 'build/../../outside.txt'
        result = self.check_manifest(manifest)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn('expected a normalized relative path', result.stderr)


if __name__ == '__main__':
    unittest.main()
