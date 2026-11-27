#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the pinned JieLi inputs and extract the toolchain for a local build."""
import argparse
import hashlib
import json
import platform
import re
import subprocess
import sys
import tarfile
from pathlib import Path

TOOLCHAIN = 'jieli-linux-toolchains-20260730.1'
ARCHIVE_URL = f'https://jl-update.oss-cn-shenzhen.aliyuncs.com/{TOOLCHAIN}.tar.xz'
ARCHIVE_SHA = 'a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd'
SDK_URL = 'https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git'
SDK_TAG = 'AC79NN_SDK_V1.2.1_2023-12-13'
SDK_COMMIT = 'd179b4484759423312073f5fbb232501aa491047'
SDK_LICENSE_SHA = 'c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4'
SDK_FILES = {
    'uboot.boot': '4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4',
    'cfg_tool.bin': '276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b',
    'cfg/eq_cfg_hw.bin': '41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d',
}


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_sdk(sdk):
    for ref in ('HEAD', f'refs/tags/{SDK_TAG}^{{commit}}'):
        result = subprocess.run(['git', '-C', str(sdk), 'rev-parse', '--verify', ref],
                                capture_output=True, text=True, check=True)
        if result.stdout.strip() != SDK_COMMIT:
            raise ValueError(f'wrong SDK revision for {ref}')
    if sha256(sdk / 'LICENSE') != SDK_LICENSE_SHA:
        raise ValueError('SDK license digest mismatch')
    files = {}
    for name, expected in SDK_FILES.items():
        actual = sha256(sdk / 'cpu/wl82/tools' / name)
        if actual != expected:
            raise ValueError(f'SDK digest mismatch for {name}')
        files[name] = actual
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('sdk', type=Path)
    parser.add_argument('destination', type=Path, help='empty extraction directory')
    args = parser.parse_args()

    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        print('BLOCKED: the pinned native toolchain requires Linux x86-64', file=sys.stderr)
        return 2
    if not args.archive.is_file() or not args.sdk.is_dir():
        print('BLOCKED: provide the pinned archive and SDK checkout', file=sys.stderr)
        return 2
    try:
        archive_sha = sha256(args.archive)
        if archive_sha != ARCHIVE_SHA:
            raise ValueError('JieLi archive digest mismatch')
        sdk_files = verify_sdk(args.sdk)
        args.destination.mkdir(parents=True, exist_ok=True)
        if any(args.destination.iterdir()):
            raise ValueError('toolchain extraction directory must be empty')
        with tarfile.open(args.archive, 'r:xz') as archive:
            archive.extractall(args.destination, filter='data')
        clang = args.destination / TOOLCHAIN / 'pi32v2/bin/clang'
        cc = subprocess.run([str(clang.resolve()), '--version'],
                            capture_output=True, text=True, check=True)
        version = cc.stdout.strip().partition('\n')[0]
        if not re.match(r'^clang version 4\.0\.1\b', version):
            raise ValueError(f'unexpected pi32v2 compiler version: {version}')
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError) as exc:
        print(f'FAIL: native input verification: {exc}', file=sys.stderr)
        return 1
    print(json.dumps({
        'toolchain': {'url': ARCHIVE_URL, 'archive_sha256': archive_sha,
                      'clang_version': version, 'clang_sha256': sha256(clang)},
        'sdk': {'url': SDK_URL, 'tag': SDK_TAG, 'commit': SDK_COMMIT,
                'license_sha256': SDK_LICENSE_SHA, 'packaging_files_sha256': sdk_files},
    }, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
