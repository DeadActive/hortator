#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the Foundation audit record; never substitute for measured evidence.
Exit 0: structurally complete, 1: invalid input, 2: build evidence blocked.
"""
import json
import sys
from pathlib import Path

PIN = '44c9ad4aa57bebf63f412bd552e57143c30b4065'
BUILD_FIELDS = ('native_build', 'host_build', 'wasm_build')
BUDGETS = ('ram_app', 'flash_app')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def nonempty(value):
    return isinstance(value, str) and bool(value.strip())


def validate(a):
    require(isinstance(a, dict), 'record must be a JSON object')
    fields = ('upstream_sha', 'step_bytes', 'project_bytes', 'var_cap',
              'integration_points', 'unknowns', *BUILD_FIELDS,
              *(b + s for b in BUDGETS for s in ('_used', '_limit')))
    for key in fields:
        require(key in a, f'missing field: {key}')
    require(a['upstream_sha'] == PIN, 'upstream_sha must match pinned source')
    for key, expected in [('step_bytes', 4), ('var_cap', 3576)]:
        require(type(a[key]) is int and a[key] == expected, f'{key}: expected {expected}')
    for key in BUILD_FIELDS:
        require(a[key] in ('pass', 'blocked', 'fail'), f'{key}: invalid status')
    native = a['native_build'] == 'pass'
    project = a['project_bytes']
    require((project is None and not native) or
            (type(project) is int and 0 < project <= 3840), 'project_bytes: invalid or unmeasured')
    for budget in BUDGETS:
        used, limit = (a[budget + s] for s in ('_used', '_limit'))
        for suffix, value in (('_used', used), ('_limit', limit)):
            require((value is None and not native) or (type(value) is int and value > 0),
                    f'{budget}{suffix}: invalid or unmeasured')
        if used is not None and limit is not None:
            require(used <= limit, f'{budget}_used exceeds {budget}_limit')
            require(used * 10 <= limit * 9, f'{budget}_used leaves less than 10% reserve')
    points = a['integration_points']
    require(isinstance(points, list) and bool(points), 'integration_points: empty or invalid')
    for p in points:
        require(isinstance(p, dict) and all(nonempty(p.get(k)) for k in ('path', 'symbol', 'owner'))
                and isinstance(p.get('callers'), list) and bool(p['callers'])
                and all(nonempty(c) for c in p['callers']), 'integration_points: incomplete entry')
    require(isinstance(a['unknowns'], list) and all(nonempty(x) for x in a['unknowns']), 'unknowns: invalid')
    if any(a[key] != 'pass' for key in BUILD_FIELDS):
        return 2
    return 0


def main(argv):
    if len(argv) != 2:
        print('usage: audit.py RECORD.json', file=sys.stderr)
        return 1
    try:
        record = json.loads(Path(argv[1]).read_text(encoding='utf-8'))
        result = validate(record)
    except (OSError, ValueError) as exc:
        print(f'AUDIT INVALID: {exc}', file=sys.stderr)
        return 1
    if result == 2:
        print('FOUNDATION BLOCKED: required build evidence incomplete; no native release')
    else:
        print('AUDIT STRUCTURE PASS: review measured budget and raw evidence separately')
    return result


if __name__ == '__main__':
    sys.exit(main(sys.argv))
