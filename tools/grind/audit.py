#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the Foundation audit record; never substitute for measured evidence.
Exit 0: structurally complete, 1: invalid input, 2: build evidence blocked.
"""
import json
import re
import sys
from pathlib import Path

PIN = '44c9ad4aa57bebf63f412bd552e57143c30b4065'
BUILD_FIELDS = ('native_build', 'host_build', 'wasm_build')
BUDGETS = ('ram_app', 'flash_app')
CALLSITE_AREAS = ('sequencer', 'cue', 'recording', 'ui_edits', 'midi_edits',
                  'editor_edits', 'project_storage', 'variation_and_lock_storage',
                  'boot_and_hal')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def nonempty(value):
    return isinstance(value, str) and bool(value.strip())


def validate(a, complete=False):
    require(isinstance(a, dict), 'record must be a JSON object')
    fields = ('upstream_sha', 'step_bytes', 'project_bytes', 'var_cap',
              'integration_points', 'unknowns', *BUILD_FIELDS,
              *(b + s for b in BUDGETS for s in ('_used', '_limit')))
    for key in fields:
        require(key in a, f'missing field: {key}')
    require(a['upstream_sha'] == PIN, 'upstream_sha must match pinned source')
    for key, expected in [('step_bytes', 4), ('var_cap', 3576)]:
        require((a[key] is None and complete and a['native_build'] != 'pass') or
                (type(a[key]) is int and a[key] == expected), f'{key}: expected {expected}')
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
    points = a['integration_points']
    require(isinstance(points, list) and (points or complete), 'integration_points: empty or invalid')
    for p in points:
        require(isinstance(p, dict) and all(nonempty(p.get(k)) for k in ('path', 'symbol', 'owner'))
                and isinstance(p.get('callers'), list) and bool(p['callers'])
                and all(nonempty(c) for c in p['callers']), 'integration_points: incomplete entry')
    require(isinstance(a['unknowns'], list) and all(nonempty(x) for x in a['unknowns']), 'unknowns: invalid')
    if complete:
        tree = a.get('firmware_tree')
        reviewed = a.get('reviewed_firmware_tree')
        require(nonempty(tree) and re.fullmatch('[0-9a-f]{40}', tree), 'firmware_tree: invalid')
        require(reviewed is None or (nonempty(reviewed) and re.fullmatch('[0-9a-f]{40}', reviewed)),
                'reviewed_firmware_tree: invalid')
        require(a.get('resource_status') in ('pass', 'blocked', 'fail'), 'resource_status: invalid')
        statuses = [a[key] for key in BUILD_FIELDS] + [a['resource_status']]
        if 'fail' in statuses:
            return 1
        areas = set()
        reviewed_symbols = set()
        for p in points:
            area = p.get('area')
            require(isinstance(area, str) and area in CALLSITE_AREAS, 'integration_points: invalid area')
            anchor = (p['path'], p['symbol'])
            require(anchor not in reviewed_symbols, 'integration_points: duplicate reviewed symbol')
            reviewed_symbols.add(anchor)
            areas.add(area)
        if reviewed != tree or areas != set(CALLSITE_AREAS):
            print('BLOCKED: current firmware call-site coverage is incomplete')
            return 2
        return 0 if all(status == 'pass' for status in statuses) and not a['unknowns'] else 2
    if any(a[key] != 'pass' for key in BUILD_FIELDS):
        return 2
    return 0


def main(argv):
    complete = len(argv) == 3 and argv[1] == '--complete'
    if len(argv) != 2 and not complete:
        print('usage: audit.py [--complete] RECORD.json', file=sys.stderr)
        return 1
    try:
        record = json.loads(Path(argv[-1]).read_text(encoding='utf-8'))
        result = validate(record, complete)
    except (OSError, ValueError) as exc:
        print(f'AUDIT INVALID: {exc}', file=sys.stderr)
        return 1
    if result == 2:
        print('BLOCKED: T03 audit evidence incomplete' if complete else
              'FOUNDATION BLOCKED: required build evidence incomplete; no native release')
    elif result == 1:
        print('FAIL: required T03 check failed')
    elif complete:
        print('PASS: complete T03 audit')
    else:
        print('AUDIT STRUCTURE PASS: review measured budget and raw evidence separately')
    return result


if __name__ == '__main__':
    sys.exit(main(sys.argv))
