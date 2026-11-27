#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Drum patterns from song excerpts (rhythm_songs.json): a rhythm reference for GRIND, and patterns for the FM-1.

  songs_pattern.py list    FILE                         the songs: tempo, what the quantizer found
  songs_pattern.py show    FILE SONG [--bar N]          a 4-bar window as the eight GRIND roles (K S H R C X TH TL)
  songs_pattern.py compare FILE [--golden DIR]          which of the 22 GRIND styles each song is nearest to, and how the
                                                        role densities differ (the styles' golden patterns, no device)
  songs_pattern.py send    FILE SONG [--bar N] [--var V] [--port RE] [--bpm] [--save]
                                                        write the window to the FM-1's loaded pattern, over USB-MIDI

FILE is a JSON object of songs, each with `bpm`, `tpb`, `tempoMap` [{tick, usPerBeat}], `excerptStartMs` and `notes`
[{t (ms in the excerpt), lane, p (MIDI note), v}]. Only the `drums` lane is used. The times are put on the song's own
tick grid through its tempo map (4/4, tick 0 a downbeat), the whole excerpt is shifted by the phase that fits the
16th grid best (an excerpt that does not start on a 16th is common), and every hit goes to the nearest 16th. What the
grid cannot hold stays as the step's NUDGE (-50..+50 % of a step), so a played groove keeps its feel.

Roles (the order GRIND writes its tracks in): GM 35 36 kick, 37..40 snare, 42 44 46 hi-hat, 51 53 59 ride, 49 55 57
crash, 52 china, 47 48 50 high tom, 41 43 45 low tom; other notes are left out (counted). A song whose velocities
differ marks its loudest hits as accents.

`send` writes the 64 steps (and each step's NUDGE) of the eight tracks, sets every track to LEN 64, 16th, no swing,
no Grids source, and with --bpm the tempo. The FM-1 must be stopped and runs this firmware or Hortator 1.3 or later
(editor protocol 1.1 with NUDGE). It does not save (the slot stays marked unsaved) unless --save is given.
Needs mido and python-rtmidi for `send` only.
"""
import argparse
import csv
import json
import re
import statistics
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROLES = ["K", "S", "H", "R", "C", "X", "TH", "TL"]
GM_ROLE = {35: 0, 36: 0, 37: 1, 38: 1, 39: 1, 40: 1, 42: 2, 44: 2, 46: 2, 51: 3, 53: 3, 59: 3,
           49: 4, 55: 4, 57: 4, 52: 5, 47: 6, 48: 6, 50: 6, 41: 7, 43: 7, 45: 7}
BAR = 16                                             # 16ths per bar (4/4)
WINDOW = 64                                          # the FM-1's steps


class SongError(Exception):
    pass


def load(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise SongError(f"{path}: {e}")
    if not isinstance(data, dict) or not data:
        raise SongError(f"{path}: expected an object of songs")
    return data


def pick(songs, name):
    """a song by name, or by its number prefix ("05"), or a unique part of the name"""
    if name in songs:
        return name, songs[name]
    hits = [k for k in songs if k.lower().startswith(name.lower()) or name.lower() in k.lower()]
    if len(hits) != 1:
        raise SongError(f"song {name!r}: {'no match' if not hits else 'several: ' + ', '.join(hits)}")
    return hits[0], songs[hits[0]]


def ticks_of(song, ms):
    """milliseconds of the song to its ticks, through the tempo map"""
    tpb = song["tpb"]
    tm = sorted(song["tempoMap"], key=lambda s: s["tick"])
    acc = 0.0                                        # ms at the start of the segment
    for i, seg in enumerate(tm):
        mspt = seg["usPerBeat"] / 1000.0 / tpb       # ms per tick
        if i + 1 < len(tm):
            seg_ms = (tm[i + 1]["tick"] - seg["tick"]) * mspt
            if ms >= acc + seg_ms:
                acc += seg_ms
                continue
        return seg["tick"] + (ms - acc) / mspt
    raise SongError("empty tempo map")


def quantize(song):
    """-> dict(events [(step, role, vel, nudge)], phase, err, dropped); steps count from the downbeat of tick 0"""
    grid = song["tpb"] / 4.0
    hits, dropped = [], 0
    for n in song["notes"]:
        if n.get("lane") != "drums":
            continue
        role = GM_ROLE.get(n["p"])
        if role is None:
            dropped += 1
            continue
        hits.append((ticks_of(song, song["excerptStartMs"] + n["t"]), role, n.get("v", 100)))
    if not hits:
        return dict(events=[], phase=0.0, err=0.0, dropped=dropped, accents=False)

    def err(ph):
        return statistics.mean(abs((t - ph) / grid - round((t - ph) / grid)) for t, _, _ in hits)

    phases = [i * grid / 120.0 for i in range(120)]
    best = min(phases, key=err)
    phase = 0.0 if err(0.0) <= err(best) + 0.01 else best      # no shift when the grid already fits
    if phase > grid / 2:
        phase -= grid                                           # the offset nearest zero: a groove a little early is
                                                                # early, not nearly a whole step late (step numbers count
                                                                # from the downbeat)
    vels = {v for _, _, v in hits}
    top = max(vels)
    events = []
    for t, role, v in hits:
        s = round((t - phase) / grid)
        if s < 0:
            continue
        res = (t - phase) - s * grid
        nudge = max(-50, min(50, round(res / grid * 100)))
        events.append((s, role, 1 if (len(vels) > 1 and v == top) else 0, nudge))
    return dict(events=events, phase=phase, err=err(phase), dropped=dropped, accents=len(vels) > 1)


def shifted(events, n):
    """the hits `n` 16ths later (earlier when negative); the grid's own downbeat is a guess for an excerpt cut from
    the middle of a song, and a window that starts one 16th off is slid back with this"""
    return [(s + n, r, a, d) for s, r, a, d in events if s + n >= 0]


def bar_counts(events):
    n = {}
    for s, _, _, _ in events:
        n[s // BAR] = n.get(s // BAR, 0) + 1
    return n


def best_bar(events, bars=4):
    """the first bar of the `bars`-bar window with the most to show: its hits, and 6 more for every role that plays in
    it (a window of one instrument is a worse reference than a kit); the earliest on a tie"""
    n = bar_counts(events)
    if not n:
        return 0

    def score(b):
        inside = {(e[0], e[1]) for e in events if b <= e[0] // BAR < b + bars}      # a step and role count once
        return len(inside) + 6 * len({r for _, r in inside})

    return max(range(min(n), max(n) + 1), key=lambda b: (score(b), -b))


def window(events, bar):
    """-> cells[role][step] = None or dict(acc, nudge), 64 steps from `bar`"""
    cells = [[None] * WINDOW for _ in ROLES]
    for s, role, acc, nudge in events:
        i = s - bar * BAR
        if 0 <= i < WINDOW:
            c = cells[role][i]
            cells[role][i] = dict(acc=acc | (c["acc"] if c else 0), nudge=c["nudge"] if c else nudge)
    return cells


def render(cells):
    out = []
    for r, row in enumerate(cells):
        line = ""
        for i, c in enumerate(row):
            if i and i % BAR == 0:
                line += "|"
            elif i and i % 4 == 0:
                line += " "
            line += "." if c is None else "X" if c["acc"] else "x"
        out.append(f"{ROLES[r]:>2}  {line}")
    return "\n".join(out)


def count(cells):
    return [sum(c is not None for c in row) for row in cells]


# ---------------------------------------------------------------- reference: the styles ---

def read_golden(folder):
    """the portable core's golden patterns: -> {style id: (name, [pattern, ...])}; pattern = tracks of
    (length, division, [step on]) as GM_ENCODED_SIZE records (serialize.c)"""
    folder = Path(folder)
    names = {}
    with open(folder / "styles-v1.csv", newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            names[int(row["id"])] = row["name"]
    raw = (folder / "patterns-v1.bin").read_bytes()
    size = 25 + 8 * 4 + 8 * 64 * 4 + 64
    if len(raw) % size:
        raise SongError("patterns-v1.bin: not a multiple of the record size")
    styles = {}
    for off in range(0, len(raw), size):
        rec = raw[off:off + size]
        if rec[:4] != b"GMC1":
            raise SongError("patterns-v1.bin: bad record")
        alg = rec[6]
        tracks = []
        for t in range(8):
            length, division = rec[25 + t * 4], rec[25 + t * 4 + 1]
            base = 25 + 32 + t * 256
            on = [bool(rec[base + s * 4] & 1) for s in range(64)]
            tracks.append((length, division, on[:length]))
        styles.setdefault(alg, (names.get(alg, str(alg)), []))[1].append(tracks)
    return styles


def features(tracks):
    """per role: hits per beat, and where in the beat they fall (4 bins); a track's own grid sets the beat"""
    dens, hist = [], []
    for length, division, on in tracks:
        spb = max(1, division // 4)                  # steps per beat
        beats = length / spb
        idx = [i for i, v in enumerate(on) if v]
        dens.append(len(idx) / beats if beats else 0.0)
        h = [0.0] * 4
        for i in idx:
            h[min(3, (i % spb) * 4 // spb)] += 1.0
        tot = sum(h)
        hist.append([x / tot for x in h] if tot else h)
    return dens, hist


def song_features(cells):
    on = [[c is not None for c in row] for row in cells]
    return features([(WINDOW, 16, row) for row in on])


def style_features(patterns):
    fs = [features(p) for p in patterns]
    dens = [statistics.mean(f[0][r] for f in fs) for r in range(8)]
    hist = [[statistics.mean(f[1][r][b] for f in fs) for b in range(4)] for r in range(8)]
    return dens, hist


def distance(a, b):
    """hits per beat of every role, plus (half weight) where the kick, snare and hi-hat fall in the beat"""
    d = sum(abs(x - y) for x, y in zip(a[0], b[0]))
    for r in (0, 1, 2):
        d += 0.5 * sum(abs(x - y) for x, y in zip(a[1][r], b[1][r]))
    return d


# ---------------------------------------------------------------- the FM-1 ---

HDR = [0x7D, 0x48, 0x54]
INFO, SET, STEPS_SET, SAVE = 1, 5, 12, 16
CAP_PATTERNS, CAP_NUDGE = 0x04, 0x20


def ids():
    """parameter ids by name, from the editor's device data (the firmware's core.h order)"""
    d = json.loads((ROOT / "web/editor/device_data.json").read_text(encoding="utf-8"))
    return {n: d["P"].index(n) for n in ("SLEN", "SDIV", "SSWING", "SRC")}, d["G"].index("BPM")


def u14(v):
    return [v & 0x7F, (v >> 7) & 0x7F]


def v14(v):
    return u14(v + 8192)


def step_bytes(c):
    return [0, 0, 60] if c is None else [1 | (2 if c["acc"] else 0), 0, 60]


def send(link, cells, bpm=None, var=None, save=False, log=print):
    """the window to the loaded pattern: the steps (+ NUDGE) of every track, then the timing knobs. The steps go first:
    a STEPS_SET for another variation makes the FM-1 switch to it (stopped), and the timing knobs belong to the
    variation that plays; a refused upload (playing, a queued slot) then changes nothing. `var` None: the playing one."""
    st = link.state() if hasattr(link, "state") else None
    if st is not None:
        if st[0] or st[6] & 0x02:
            raise RuntimeError("the FM-1 is playing (or a song runs): stop it first")
        if st[6] & 0x01:
            raise RuntimeError("the FM-1 is restoring a backup: wait for it")
        if var is None:
            var = st[2]
    var = 0 if var is None else var
    if not any(c is not None for row in cells for c in row):
        raise RuntimeError("that window holds no hits: nothing written")
    pid, gbpm = ids()
    for t, row in enumerate(cells):
        args = [0x7F, var, t, 0, WINDOW]
        for c in row:
            args += step_bytes(c)
        args += [(c["nudge"] if c else 0) + 64 for c in row]
        link.req(STEPS_SET, args)
        log(f"  {ROLES[t]:>2}: {sum(c is not None for c in row)} hits")
    for t in range(8):
        for name, val in (("SLEN", 64), ("SDIV", 2), ("SSWING", 0), ("SRC", 0)):
            link.req(SET, [0, t] + u14(pid[name]) + v14(val))
    if bpm:
        link.req(SET, [1, 0] + u14(gbpm) + v14(int(round(bpm))))
    if save:
        link.req(SAVE, [0x7F])


def pack_str(b, i):
    j = b.index(0, i)
    return bytes(b[i:j]).decode("ascii", "replace"), j + 1


class Link:
    def __init__(self, port=None):
        try:
            import mido
        except ImportError:
            sys.exit("needs mido: pip install mido python-rtmidi")
        self.mido, self.seq = mido, 0
        pat = re.compile(port or r"hortator|felucca|fm-1|clipper", re.I)
        outs = [n for n in mido.get_output_names() if pat.search(n) and not re.search(r"update", n, re.I)]
        ins = [n for n in mido.get_input_names() if pat.search(n) and not re.search(r"update", n, re.I)]
        if not outs or not ins:
            sys.exit("no FM-1 MIDI port (try --port NAME): connect the FM-1 by USB")
        self.o, self.i = mido.open_output(outs[0]), mido.open_input(ins[0])
        r = self.req(INFO, [], 1.0)
        self.info = r
        magic, k = pack_str(r, 0)
        fw, k = pack_str(r, k)
        minor, caps = r[k + 1], r[k + 2] | r[k + 3] << 7
        if magic != "HORTATOR" or not caps & CAP_PATTERNS:
            sys.exit("this FM-1's firmware has no pattern editing over USB")
        if not caps & CAP_NUDGE:
            sys.exit(f"firmware {fw} (protocol minor {minor}) has no NUDGE: update it first")

    def state(self):
        """the state block (§3), fresh: the last 17 bytes of INFO's reply"""
        self.info = self.req(INFO, [], 1.0)
        return list(self.info[-17:])

    def req(self, cmd, args, timeout=2.0, tries=3):
        self.seq = (self.seq + 1) % 127
        for _ in range(tries):
            for _m in self.i.iter_pending():
                pass
            self.o.send(self.mido.Message("sysex", data=HDR + [cmd, self.seq] + list(args)))
            t0 = time.time()
            while time.time() - t0 < timeout:
                for m in self.i.iter_pending():
                    d = list(m.data) if m.type == "sysex" else []
                    if d[:5] == HDR + [cmd, self.seq]:
                        if d[5]:
                            raise RuntimeError(f"refused: {pack_str(d, 7)[0]} (rc {d[5]}, detail {d[6]})")
                        return d[6:]
                time.sleep(0.002)
        raise TimeoutError(f"no reply to command {cmd}")


# ---------------------------------------------------------------- the commands ---

def cmd_list(a):
    for name, s in load(a.file).items():
        q = quantize(s)
        hits = len(q["events"])
        bar = best_bar(q["events"])
        grid = "fits the 16th grid" if q["err"] < 0.05 else f"loose (mean {q['err']:.2f} of a 16th off the grid)"
        print(f"{name}: {s['bpm']:g} BPM, {hits} hits, {q['dropped']} left out, phase {q['phase']:.0f} ticks, {grid}; "
              f"densest 4 bars from bar {bar + 1}")


def cmd_show(a):
    name, s = pick(load(a.file), a.song)
    q = quantize(s)
    q["events"] = shifted(q["events"], a.shift)
    bar = a.bar - 1 if a.bar else best_bar(q["events"])
    cells = window(q["events"], bar)
    print(f"{name}: {s['bpm']:g} BPM, bars {bar + 1}..{bar + 4}  (x hit, X accent)")
    print(render(cells))
    print("hits per role:", dict(zip(ROLES, count(cells))))


def cmd_compare(a):
    songs = load(a.file)
    styles = read_golden(a.golden)
    sf = {k: style_features(v[1]) for k, v in styles.items()}
    allsong = []
    for name, s in songs.items():
        q = quantize(s)
        cells = window(q["events"], best_bar(q["events"]))
        f = song_features(cells)
        allsong.append(f)
        ranked = sorted(sf, key=lambda k: distance(f, sf[k]))
        print(f"{name} ({s['bpm']:g} BPM)")
        print("   nearest: " + ", ".join(f"{styles[k][0]} {distance(f, sf[k]):.2f}" for k in ranked[:3]))
        print("   hits/beat  " + "  ".join(f"{ROLES[r]} {f[0][r]:.2f}" for r in range(8)))
    mean = [statistics.mean(f[0][r] for f in allsong) for r in range(8)]
    sty = [statistics.mean(v[0][r] for v in sf.values()) for r in range(8)]
    print("\nmean hits per beat, songs vs all 22 styles")
    print("   songs   " + "  ".join(f"{ROLES[r]} {mean[r]:.2f}" for r in range(8)))
    print("   styles  " + "  ".join(f"{ROLES[r]} {sty[r]:.2f}" for r in range(8)))


def cmd_send(a):
    name, s = pick(load(a.file), a.song)
    q = quantize(s)
    q["events"] = shifted(q["events"], a.shift)
    bar = a.bar - 1 if a.bar else best_bar(q["events"])
    cells = window(q["events"], bar)
    print(f"{name}: bars {bar + 1}..{bar + 4}, {s['bpm']:g} BPM")
    print(render(cells))
    if not any(c is not None for row in cells for c in row):
        raise SongError(f"bars {bar + 1}..{bar + 4} hold no hits (the excerpt has {max(bar_counts(q['events']), default=-1) + 1} bars)")
    send(Link(a.port), cells, bpm=s["bpm"] if a.bpm else None, var=a.var, save=a.save)
    print("written" + (" and saved" if a.save else " (not saved: SAVE on the FM-1, or --save)"))


def bar_arg(s):
    try:
        v = int(s)
    except ValueError:
        v = 0
    if v < 1:
        raise argparse.ArgumentTypeError("a bar counts from 1")
    return v


def var_arg(s):
    try:
        v = int(s)
    except ValueError:
        v = -1
    if not 0 <= v <= 7:
        raise argparse.ArgumentTypeError("a variation is 0..7")
    return v


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.add_argument("file")
    p.set_defaults(fn=cmd_list)
    p = sub.add_parser("show")
    p.add_argument("file")
    p.add_argument("song")
    p.add_argument("--bar", type=bar_arg, help="first bar of the window, counted from 1 (default: the densest)")
    p.add_argument("--shift", type=int, default=0, help="slide the hits by N 16ths (e.g. -1) if the bar line looks off")
    p.set_defaults(fn=cmd_show)
    p = sub.add_parser("compare")
    p.add_argument("file")
    p.add_argument("--golden", default=str(ROOT / "tests/grind/golden"))
    p.set_defaults(fn=cmd_compare)
    p = sub.add_parser("send")
    p.add_argument("file")
    p.add_argument("song")
    p.add_argument("--bar", type=bar_arg)
    p.add_argument("--shift", type=int, default=0, help="slide the hits by N 16ths (e.g. -1)")
    p.add_argument("--var", type=var_arg, help="variation of the loaded pattern, 0..7 (default: the playing one)")
    p.add_argument("--port", help="regex for the FM-1's MIDI port name")
    p.add_argument("--bpm", action="store_true", help="also set the song's tempo")
    p.add_argument("--save", action="store_true", help="save the slot after writing")
    p.set_defaults(fn=cmd_send)
    a = ap.parse_args(argv)
    try:
        a.fn(a)
    except (SongError, RuntimeError, TimeoutError) as e:
        sys.exit(f"songs_pattern: {e}")


if __name__ == "__main__":
    main()
