# SPDX-License-Identifier: GPL-3.0-only
"""Test asset license decisions with independent inputs and verify the checked-in inventory."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path('assets/grind/manifest.json')
CLI = Path('tools/grind/check_assets.py')

sys.path.insert(0, str(ROOT / CLI.parent))
from check_assets import REQUIRED_GENERATED, REQUIRED_SDK, REQUIRED_SOURCES  # noqa: E402

SAMPLE = 'media/fixture.wav'
NOTICE = 'fixture-license.txt'
PIN = '44c9ad4aa57bebf63f412bd552e57143c30b4065'


def run_gate(root, *options):
    env = os.environ.copy()
    env.pop('AC79_SDK', None)
    return subprocess.run([sys.executable, str(root / CLI), *options],
                          env=env, capture_output=True, text=True)


class CheckAssetsTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name) / 'repo'
        self.sdk = Path(scratch.name) / 'sdk'

        for path in (CLI, CLI.with_name('audit.py')):
            destination = self.root / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / path, destination)

        assets = []
        for kind, base, paths in (
            ('tracked', self.root, REQUIRED_SOURCES | {SAMPLE}),
            ('generated', self.root, REQUIRED_GENERATED),
            ('sdk', self.sdk, REQUIRED_SDK),
        ):
            for path in sorted(paths):
                file = base / path
                file.parent.mkdir(parents=True, exist_ok=True)
                content = f'Synthetic {kind} asset at {path}.'.encode()
                file.write_bytes(content)
                assets.append({
                    'path': path,
                    'kind': kind,
                    'origin': 'Synthetic test asset',
                    'license': 'MIT',
                    'status': 'cleared',
                    'sha256': hashlib.sha256(content).hexdigest(),
                    'notices': [NOTICE],
                })

        (self.root / NOTICE).write_text('Synthetic license notice.', encoding='utf-8')
        subprocess.run(['git', 'init', '-q'], cwd=self.root, check=True)
        subprocess.run(['git', 'add', '--', NOTICE, SAMPLE, *sorted(REQUIRED_SOURCES)],
                       cwd=self.root, check=True)

        self.manifest = {
            'schema_version': 1,
            'upstream_sha': PIN,
            'release_approved': True,
            'open_reviews': [],
            'assets': assets,
        }

    def asset(self, kind, path=None):
        return next(item for item in self.manifest['assets']
                    if item['kind'] == kind and (path is None or item['path'] == path))

    def check_manifest(self, *options):
        path = self.root / MANIFEST
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.manifest), encoding='utf-8')
        return run_gate(self.root, *options)

    def test_approved_complete_fixture_passes(self):
        result = self.check_manifest('--sdk-root', str(self.sdk))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS: asset license gate', result.stdout)

    def test_approval_cannot_bypass_a_restricted_asset(self):
        self.asset('tracked', SAMPLE).update(
            status='restricted', license='LicenseRef-All-Rights-Reserved')
        result = self.check_manifest('--sdk-root', str(self.sdk))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(f'{SAMPLE}: restricted', result.stdout)

    def test_source_gate_accepts_cleared_sources_without_release_evidence(self):
        self.manifest.update(release_approved=False, open_reviews=['Synthetic open review'])
        generated = self.asset('generated')
        generated.update(status='review_pending', sha256=None)
        (self.root / generated['path']).unlink()

        result = self.check_manifest('--source-only')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS: source asset license gate', result.stdout)

        result = self.check_manifest('--sdk-root', str(self.sdk))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn('release approval is pending', result.stdout)

    def test_modified_tracked_bytes_fail(self):
        (self.root / SAMPLE).write_bytes(b'Modified asset bytes.')
        result = self.check_manifest('--source-only')
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(f'{SAMPLE}: SHA-256 mismatch', result.stderr)

    def test_omitting_required_assets_fails(self):
        assets = self.manifest['assets']
        for kind, target in (
            ('tracked', SAMPLE),
            ('generated', self.asset('generated')['path']),
            ('sdk', self.asset('sdk')['path']),
        ):
            with self.subTest(kind=kind):
                self.manifest['assets'] = [asset for asset in assets
                                           if (asset['kind'], asset['path']) != (kind, target)]
                result = self.check_manifest('--source-only')
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn(f'missing {kind} inventory entries: {target}', result.stderr)

    def test_generated_bytes_missing_despite_approval(self):
        generated = self.asset('generated')['path']
        (self.root / generated).unlink()
        result = self.check_manifest('--sdk-root', str(self.sdk))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(f'{generated}: matching artifact evidence is missing', result.stdout)

    def test_mismatched_sdk_bytes_fail(self):
        sdk_asset = self.asset('sdk')['path']
        (self.sdk / sdk_asset).write_bytes(b'Modified SDK bytes.')
        result = self.check_manifest('--sdk-root', str(self.sdk))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(f'{sdk_asset}: SHA-256 mismatch', result.stderr)

    def test_traversal_is_rejected(self):
        self.asset('generated')['path'] = 'build/../../outside.txt'
        result = self.check_manifest('--source-only')
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn('expected a normalized relative path', result.stderr)


class CheckedInAssetInventoryTests(unittest.TestCase):
    def test_current_manifest_has_consistent_source_review_and_digests(self):
        manifest = json.loads((ROOT / MANIFEST).read_text(encoding='utf-8'))
        sources = [asset for asset in manifest['assets'] if asset['kind'] == 'tracked']
        ready = all(asset['status'] == 'cleared' for asset in sources)

        result = run_gate(ROOT, '--source-only')
        self.assertEqual(result.returncode, 0 if ready else 2, result.stdout + result.stderr)
        status = 'PASS' if ready else 'BLOCKED'
        self.assertIn(f'{status}: source asset license gate ({len(sources)} file digests verified)',
                      result.stdout)


if __name__ == '__main__':
    unittest.main()
