#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect current native facts and assemble Foundation T03 evidence."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

from audit import BUDGETS, CALLSITE_AREAS, PIN, require
from native_inputs import sha256

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'build/grind-evidence'
LAYOUT = {'step_bytes': 'sizeof(step_t)', 'project_bytes': 'sizeof(project_t)',
          'var_cap': 'VAR_CAP'}
REVIEW = ROOT / 'docs/grind/callsite-audit.json'


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def symbols(path):
    listing = subprocess.check_output(['readelf', '-sW', str(path)], cwd=ROOT, text=True)
    sizes = {}
    for line in listing.splitlines():
        parts = line.split()
        if len(parts) == 8 and parts[3] == 'OBJECT' and parts[2].isdigit():
            sizes[parts[-1]] = int(parts[2])
    return sizes


def native_facts(resource_path):
    sys.path.insert(0, str(ROOT / 'tools'))
    import build

    require(not git('status', '--porcelain', '--', 'firmware'),
            'native audit requires the committed firmware sources')
    resources = json.loads(resource_path.read_text(encoding='utf-8'))
    static_status = resources['static_status'].lower()
    require(static_status in ('pass', 'blocked', 'fail'), 'invalid static resource status')
    checks = resources['checks']
    require(checks['memory_evidence']['status'] == 'PASS', 'native memory evidence unavailable')
    for name, path in (('app_elf', 'build/felucca.elf'), ('app_image', 'build/felucca.bin')):
        require(resources['artifacts'][name]['sha256'] == sha256(ROOT / path),
                f'{name}: resource report does not match current native artifact')
    with TemporaryDirectory(dir=EVIDENCE) as directory:
        source = Path(directory) / 'layout.c'
        obj = source.with_suffix('.o')
        declarations = [f'const unsigned char grind_audit_{key}[{expression}] = {{0}};'
                        for key, expression in LAYOUT.items()]
        source.write_text('#include "felucca.c"\n' + '\n'.join(declarations) + '\n', encoding='utf-8')
        core_ref = os.environ.get('FELUCCA_CORE_REF') == '1'
        require(not core_ref, 'native layout audit requires the plain app build')
        build.tc('cc', *build.app_flags(False, core_ref), '-c', source, '-o', obj)
        sizes = symbols(obj)
    facts = {key: sizes[f'grind_audit_{key}'] for key in LAYOUT}
    app_symbols = symbols(ROOT / 'build/felucca.elf')
    require(app_symbols.get('proj_slot') == 4 * facts['project_bytes'],
            'native project record size disagrees with the app ELF')
    for budget in BUDGETS:
        for measurement in ('used', 'limit'):
            facts[f'{budget}_{measurement}'] = checks[budget][f'{measurement}_bytes']
    return dict(facts, revision=git('rev-parse', 'HEAD'), resource_status=static_status)


def lane_status(name):
    status = os.environ.get(name, '').lower()
    return {'success': 'pass', 'pass': 'pass', 'failure': 'fail', 'fail': 'fail'}.get(
        status, 'blocked')


def assemble():
    revision = git('rev-parse', 'HEAD')
    tree = git('rev-parse', 'HEAD:firmware')
    native = json.loads(os.environ.get('T03_NATIVE') or 'null')
    facts = {key: None for key in LAYOUT}
    facts.update({f'{budget}_{key}': None for budget in BUDGETS for key in ('used', 'limit')})
    unknowns = []
    native_status = 'blocked'
    resource_status = 'blocked'
    if native is None:
        unknowns.append('Current native layout and resource measurements are unavailable')
        if lane_status('NATIVE') == 'fail':
            native_status = 'fail'
    else:
        require(isinstance(native, dict) and native.get('revision') == revision,
                'native evidence revision does not match the current checkout')
        facts.update({key: native[key] for key in facts})
        native_status = 'pass'
        resource_status = native['resource_status']

    reviewed_tree = None
    points = []
    if REVIEW.is_file():
        review = json.loads(REVIEW.read_text(encoding='utf-8'))
        reviewed_tree = review['firmware_tree']
        points = review['integration_points']
        require(isinstance(points, list) and all(isinstance(point, dict) for point in points),
                'reviewed integration_points must be a list of objects')
        if reviewed_tree == tree:
            tracked = set(git('ls-tree', '-r', '--name-only', 'HEAD', '--', 'firmware').splitlines())
            require(all(isinstance(point.get('path'), str) and point['path'] in tracked for point in points),
                    'call-site review must reference tracked firmware paths')
    if reviewed_tree != tree or {p.get('area') for p in points} != set(CALLSITE_AREAS):
        unknowns.append('Complete reviewed call-site coverage for this firmware tree is missing')

    return dict(facts, revision=revision, upstream_sha=PIN,
                native_build=native_status, host_build=lane_status('HOST_UPSTREAM'),
                wasm_build=lane_status('WASM'), resource_status=resource_status,
                firmware_tree=tree, reviewed_firmware_tree=reviewed_tree,
                integration_points=points, unknowns=unknowns)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, help='collect from the current native build')
    args = parser.parse_args()
    try:
        print(json.dumps(native_facts(args.resources) if args.resources else assemble(), indent=2))
    except (OSError, RuntimeError, KeyError, TypeError, ValueError, subprocess.CalledProcessError) as error:
        print(f'FAIL: T03 evidence: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
