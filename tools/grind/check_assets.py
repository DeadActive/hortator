#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check P01 asset licensing evidence. Exit 0: PASS, 1: FAIL, 2: BLOCKED."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

from audit import PIN, nonempty, require

ROOT = Path(__file__).resolve().parents[2]
REQUIRED_GENERATED = {
    'build/felucca-UNTESTED.fwsc',
    'build/gen/felucca_font.h', 'build/gen/felucca_samples.h',
    'build/sim/fm1sim.wasm', 'build/sim/features.bin.gz',
    'build/sim/reel.bin.gz', 'build/sim/source.tar.gz',
    'build/sim/terminus.woff',
}
REQUIRED_SDK = {
    'LICENSE',
    'cpu/wl82/tools/uboot.boot', 'cpu/wl82/tools/cfg_tool.bin',
    'cpu/wl82/tools/cfg/eq_cfg_hw.bin',
}
REQUIRED_SOURCES = {
    'firmware/src/comp.c', 'firmware/src/comp_tables.h', 'firmware/src/dm_dsp.c',
    'firmware/src/dm_kick.c', 'firmware/src/dm_metal.c', 'firmware/src/dm_snare.c',
    'firmware/src/grids.c', 'firmware/src/grids_tables.h', 'firmware/src/phys_dsp.c',
    'tools/gen_waves.py', 'tools/rescue/fm1_uboot.py',
}
MEDIA_EXTENSIONS = {
    '.aiff', '.avif', '.bdf', '.bmp', '.flac', '.gif', '.ico', '.jpeg', '.jpg',
    '.mp3', '.mp4', '.ogg', '.otf', '.png', '.svg', '.ttf', '.wav', '.webm',
    '.webp', '.woff', '.woff2',
}
REVIEW_STATES = {'cleared', 'review_pending', 'restricted'}
UNCLEAR_LICENSES = {'NOASSERTION', 'LicenseRef-All-Rights-Reserved'}


def relative_path(value):
    require(nonempty(value), 'asset and notice paths must be nonempty strings')
    path = PurePosixPath(value)
    require(not path.is_absolute() and '..' not in path.parts and
            value == path.as_posix() and value != '.' and '\\' not in value,
            f'{value}: expected a normalized relative path')
    return value


def local_file(root, path):
    file = root / path
    require(file.resolve().is_relative_to(root.resolve()),
            f'{path}: file resolves outside its root')
    return file


def tracked_files(root):
    result = subprocess.run(['git', 'ls-files', '-z'], cwd=root, check=True,
                            capture_output=True)
    return {os.fsdecode(path) for path in result.stdout.split(b'\0') if path}


def check_digest(file, digest, label):
    actual = hashlib.sha256()
    with file.open('rb') as content:
        for chunk in iter(lambda: content.read(1024 * 1024), b''):
            actual.update(chunk)
    require(actual.hexdigest() == digest, f'{label}: SHA-256 mismatch')


def validate(manifest, root, sdk_root, source_only=False):
    require(isinstance(manifest, dict), 'manifest must be a JSON object')
    require(type(manifest.get('schema_version')) is int and
            manifest['schema_version'] == 1, 'unsupported manifest schema')
    require(manifest.get('upstream_sha') == PIN, 'upstream_sha must match pinned source')
    require(type(manifest.get('release_approved')) is bool, 'invalid release_approved')
    assets, reviews = manifest.get('assets'), manifest.get('open_reviews')
    require(isinstance(assets, list) and assets, 'assets must be a nonempty list')
    require(isinstance(reviews, list) and all(nonempty(item) for item in reviews),
            'open_reviews must be a list of nonempty strings')
    blockers = []
    if not source_only:
        blockers.extend(reviews)
        if not manifest['release_approved']:
            blockers.append('release approval is pending')
    tracked = tracked_files(root)
    inventory = {'tracked': set(), 'generated': set(), 'sdk': set()}
    checked = 0
    for asset in assets:
        require(isinstance(asset, dict), 'each asset must be a JSON object')
        path = relative_path(asset.get('path'))
        kind, status = asset.get('kind'), asset.get('status')
        require(isinstance(kind, str) and kind in inventory, f'{path}: invalid kind')
        require(path not in inventory[kind], f'{path}: duplicate {kind} entry')
        inventory[kind].add(path)
        require(isinstance(status, str) and status in REVIEW_STATES, f'{path}: invalid status')
        require(all(nonempty(asset.get(key)) for key in ('origin', 'license')),
                f'{path}: origin and license are required')
        notices = asset.get('notices')
        require(isinstance(notices, list) and notices, f'{path}: expected license notices')
        for notice in notices:
            notice = relative_path(notice)
            require(notice in tracked and local_file(root, notice).is_file(),
                    f'{path}: missing tracked notice {notice}')
        digest = asset.get('sha256')
        require((digest is None and kind == 'generated') or
                (isinstance(digest, str) and re.fullmatch('[a-f0-9]{64}', digest)),
                f'{path}: invalid SHA-256')
        if status == 'cleared':
            require(asset['license'] not in UNCLEAR_LICENSES,
                    f'{path}: cleared asset requires a license that permits distribution')
        elif kind == 'tracked' or not source_only:
            blockers.append(f'{path}: {status}')
        if kind == 'tracked':
            require(path in tracked, f'{path}: asset is not tracked')
            check_digest(local_file(root, path), digest, path)
            checked += 1
            continue
        if kind == 'generated':
            require(path.startswith('build/'), f'{path}: generated asset must be under build/')
            file = local_file(root, path)
        else:
            file = local_file(sdk_root, path) if sdk_root is not None else None
        if source_only:
            continue
        if file is not None and file.is_file() and digest:
            check_digest(file, digest, path)
            checked += 1
        elif status == 'cleared':
            blockers.append(f'{path}: matching artifact evidence is missing')

    required_tracked = REQUIRED_SOURCES | {
        path for path in tracked
        if PurePosixPath(path).suffix.lower() in MEDIA_EXTENSIONS or
        (path.startswith(('assets/fonts/', 'assets/samples-cc0/'))
         and not path.endswith(('-LICENSE.txt', 'ATTRIBUTION.txt', 'CREDITS.txt')))
    }
    for kind, required in (('tracked', required_tracked), ('generated', REQUIRED_GENERATED),
                           ('sdk', REQUIRED_SDK)):
        missing = sorted(required - inventory[kind])
        require(not missing, f'missing {kind} inventory entries: {", ".join(missing)}')
    return checked, blockers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=ROOT / 'assets/grind/manifest.json')
    sdk = os.environ.get('AC79_SDK')
    parser.add_argument('--sdk-root', type=Path, default=Path(sdk) if sdk else None,
                        help='AC79 SDK checkout (defaults to AC79_SDK)')
    parser.add_argument('--source-only', action='store_true',
                        help='check source licenses in a clean checkout')
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
        checked, blockers = validate(manifest, ROOT, args.sdk_root, args.source_only)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'FAIL: asset license inventory: {error}', file=sys.stderr)
        return 1
    gate = 'source asset license gate' if args.source_only else 'asset license gate'
    if blockers:
        print(f'BLOCKED: {gate} ({checked} file digests verified)')
        for reason in blockers:
            print(f'  - {reason}')
        return 2
    print(f'PASS: {gate} ({checked} file digests verified)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
