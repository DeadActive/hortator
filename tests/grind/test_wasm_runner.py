# SPDX-License-Identifier: GPL-3.0-only
"""Check the standalone runner's compiler and Node invocation; real WASM runs in CI."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WasmRunnerTests(unittest.TestCase):
    def run_fixture(self, suite, fixture):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in ('tests/grind/golden', 'tools/grind', 'bin'):
                (root / path).mkdir(parents=True)
            shutil.copyfile(ROOT / 'tests/run_grind_tests.sh', root / 'tests/run_grind_tests.sh')
            (root / 'tests/grind/golden/rng-v1.txt').write_text('fixture\n', encoding='utf-8')
            (root / 'tests/grind/golden/patterns-v1.bin').write_bytes(b'fixture')
            (root / 'tests/grind/golden/styles-v1.csv').write_text('fixture\n', encoding='utf-8')
            (root / 'tools/grind/rng_reference.py').write_text('print("fixture")\n', encoding='utf-8')
            (root / 'tools/grind/check_vectors.py').write_text('pass\n', encoding='utf-8')
            compiler = root / 'bin/emcc'
            compiler.write_text(
                '#!/bin/sh\nset -e\nprintf "%s\\n" "$@" > "$PWD/compiler-args"\n'
                'while [ "$#" -gt 0 ]; do\n'
                '  if [ "$1" = -o ]; then shift; printf "#!/bin/sh\\nexit 0\\n" > "$1"; chmod +x "$1"; break; fi\n'
                '  shift\n'
                'done\n', encoding='utf-8')
            node = root / 'bin/node'
            node.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$PWD/node-args"\n', encoding='utf-8')
            compiler.chmod(0o755)
            node.chmod(0o755)
            result = subprocess.run(['sh', 'tests/run_grind_tests.sh', suite], cwd=root,
                                    env=dict(os.environ, PATH=str(root / 'bin') + os.pathsep + os.environ['PATH'],
                                             CC='emcc', NODE='node', GRIND_WASM='1', GRIND_SAN_FLAGS=''),
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            args = (root / 'compiler-args').read_text(encoding='utf-8').splitlines()
            self.assertIn('--embed-file', args)
            self.assertIn(f'tests/grind/golden/{fixture}@/tests/grind/golden/{fixture}', args)
            self.assertIn(f'build/grind-wasm/test_{suite}.js', args)
            self.assertEqual((root / 'node-args').read_text(encoding='utf-8').strip(),
                             f'build/grind-wasm/test_{suite}.js')

    def test_rng_fixture(self):
        self.run_fixture('rng', 'rng-v1.txt')

    def test_pattern_fixture(self):
        self.run_fixture('golden', 'patterns-v1.bin')

    def test_style_fixture(self):
        self.run_fixture('rules', 'styles-v1.csv')


if __name__ == '__main__':
    unittest.main()
