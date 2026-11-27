# SPDX-License-Identifier: GPL-3.0-only
"""Exercise historical cache validation against a synthetic Git reference."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HistoricalCacheTests(unittest.TestCase):
    def test_toolchain_change_invalidates_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                return subprocess.check_output(['git', *args], cwd=root, text=True).strip()
            git('init', '-q')
            git('config', 'user.name', 'Test')
            git('config', 'user.email', 'test@example.invalid')
            build = root / 'build.sh'
            build.write_text('#!/bin/sh\nset -e\nmkdir -p build\n'
                             'for f in felucca.dis felucca.elf text.bin data.bin ramtext.bin; do\n'
                             '  printf "artifact\\n" > "build/$f"\ndone\n', encoding='utf-8')
            build.chmod(0o755)
            git('add', 'build.sh')
            git('commit', '-qm', 'reference')
            pin = git('rev-parse', 'HEAD')
            (root / 'tools').mkdir()
            shutil.copyfile(ROOT / 'tools/upstream_build.sh', root / 'tools/upstream_build.sh')
            (root / 'tools/check_untouched.py').write_text(f'print({pin!r})\n', encoding='utf-8')
            (root / '.venv/bin').mkdir(parents=True)
            (root / '.venv/bin/python').symlink_to(sys.executable)
            tc = root / 'toolchain'
            for name in ('pi32v2/bin/clang', 'pi32v2/bin/ld', 'common/bin/objdump'):
                file = tc / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(b'toolchain fixture')
                file.chmod(0o755)
            env = dict(os.environ, JIELI_TOOLCHAIN=str(tc), JIELI_DOCKER='0', PYTHON=sys.executable)
            def run(*args):
                return subprocess.run(['sh', 'tools/upstream_build.sh', *args], cwd=root, env=env,
                                      capture_output=True, text=True)
            result = run()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(run('--verify').returncode, 0)
            (tc / 'pi32v2/bin/clang').write_bytes(b'changed toolchain fixture')
            result = run('--verify')
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('H2/H3 need an unchanged build', result.stderr)
            result = run()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(run('--verify').returncode, 0)


if __name__ == '__main__':
    unittest.main()
