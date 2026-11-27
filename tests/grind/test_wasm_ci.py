# SPDX-License-Identifier: GPL-3.0-only
"""Exercise independent Core, preview and browser statuses in the actual CI wrapper."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WasmCiTests(unittest.TestCase):
    def run_lane(self, core_exit, preview_exit):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('tools/grind', 'tests', 'bin', 'build/grind-browser/node_modules/playwright-core'):
                (root / name).mkdir(parents=True)
            shutil.copyfile(ROOT / 'tools/grind/run_ci.sh', root / 'tools/grind/run_ci.sh')
            (root / 'tests/run_grind_tests.sh').touch()
            (root / 'tools/build_sim.sh').write_text(f'exit {preview_exit}\n', encoding='utf-8')
            (root / 'build/grind-browser/node_modules/playwright-core/index.mjs').touch()
            for name, script in (('docker', f'exit {core_exit}\n'), ('chromium', 'exit 0\n'),
                                 ('node', 'printf "%s\\n" "$1" >> "$PWD/node-invocations"\n')):
                path = root / 'bin' / name
                path.write_text('#!/bin/sh\n' + script, encoding='utf-8')
                path.chmod(0o755)
            output = root / 'status'
            result = subprocess.run(['bash', 'tools/grind/run_ci.sh', 'wasm'], cwd=root,
                                    env=dict(os.environ, PATH=str(root / 'bin') + os.pathsep + os.environ['PATH'],
                                             GITHUB_OUTPUT=str(output), GRIND_CHROMIUM=str(root / 'bin/chromium')),
                                    capture_output=True, text=True)
            return result, output.read_text(encoding='utf-8').strip(), (root / 'node-invocations').read_text().splitlines()

    def test_failed_core_does_not_skip_successful_preview_browser(self):
        result, status, invoked = self.run_lane(1, 0)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(status, 'status=FAIL')
        self.assertIn('tests/grind/browser_smoke.mjs', invoked)
        self.assertIn('PASS: browser-smoke', result.stdout)

    def test_failed_preview_blocks_browser(self):
        result, status, invoked = self.run_lane(0, 1)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(status, 'status=FAIL')
        self.assertNotIn('tests/grind/browser_smoke.mjs', invoked)
        self.assertIn('BLOCKED: browser smoke requires successful WASM preview build', result.stdout)


if __name__ == '__main__':
    unittest.main()
