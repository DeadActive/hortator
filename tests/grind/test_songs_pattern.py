# SPDX-License-Identifier: GPL-3.0-only
"""tools/songs_pattern.py on made-up songs (no real song data in the repository): the tick grid through a tempo map,
the phase fit, the roles, NUDGE, the window, the golden style reference and the bytes written to an FM-1."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import songs_pattern as sp  # noqa: E402

SIXTEENTH_MS = 125.0                                # 120 BPM, 480 ticks per quarter


def song(hits, start_ms=0.0, tempo=None, vel=100):
    """hits: (step, GM note) on a 120 BPM grid; tempo overrides the map"""
    return {"bpm": 120, "tpb": 480, "tempoMap": tempo or [{"tick": 0, "usPerBeat": 500000}],
            "excerptStartMs": start_ms, "durationMs": 30000,
            "notes": [{"t": s * SIXTEENTH_MS, "lane": "drums", "p": p, "v": vel} for s, p in hits]}


def four_bars(base=0):
    """a rock beat, four bars: kick 1 and 3, snare 2 and 4, eighth-note hats"""
    h = []
    for bar in range(4):
        for beat in range(4):
            s = base + bar * 16 + beat * 4
            h.append((s, 36 if beat in (0, 2) else 38))
            h += [(s, 42), (s + 2, 42)]
    return h


class Quantize(unittest.TestCase):
    def test_roles_and_steps(self):
        q = sp.quantize(song(four_bars()))
        self.assertEqual(q["phase"], 0.0)
        self.assertLess(q["err"], 0.001)
        cells = sp.window(q["events"], 0)
        self.assertEqual(sp.count(cells), [8, 8, 32, 0, 0, 0, 0, 0])
        self.assertTrue(cells[0][0] and cells[0][8] and not cells[0][4])      # kick: beats 1 and 3
        self.assertTrue(cells[1][4] and cells[1][12])                         # snare: 2 and 4
        self.assertTrue(cells[2][2] and cells[2][0])                          # hats on every eighth

    def test_an_excerpt_that_starts_off_the_grid_is_shifted_back(self):
        q = sp.quantize(song(four_bars(), start_ms=SIXTEENTH_MS / 2))         # half a 16th late
        self.assertAlmostEqual(q["phase"], 60.0, places=3)
        self.assertEqual(sorted((s, r) for s, r, _, _ in q["events"]),
                         sorted((s, r) for s, r, _, _ in sp.quantize(song(four_bars()))["events"]))

    def test_what_the_grid_cannot_hold_is_the_nudge(self):
        s = song(four_bars())                                                 # (the rest of the bar holds the grid)
        s["notes"][3]["t"] += SIXTEENTH_MS * 0.2                              # the snare of beat 2: 20 % late
        s["notes"][6]["t"] -= SIXTEENTH_MS * 0.9                              # the kick of beat 3: at 7.1, nearer step 7
        q = sp.quantize(s)
        self.assertEqual(q["phase"], 0.0)
        by = {(e[0], e[1]): e[3] for e in q["events"]}
        self.assertEqual(by[(0, 0)], 0)
        self.assertEqual(by[(4, 1)], 20)
        self.assertEqual(by[(7, 0)], 10)                                      # step 7, 10 % late

    def test_a_tempo_change_keeps_the_hits_on_their_steps(self):
        # bar 1 at 120 BPM, then twice as fast: the 16ths are 62.5 ms from tick 1920
        tempo = [{"tick": 0, "usPerBeat": 500000}, {"tick": 1920, "usPerBeat": 250000}]
        notes = [{"t": 0.0, "lane": "drums", "p": 36, "v": 100}, {"t": 8 * 125.0, "lane": "drums", "p": 38, "v": 100},
                 {"t": 2000.0 + 4 * 62.5, "lane": "drums", "p": 36, "v": 100}]     # bar 2, step 16 + 4
        s = {"bpm": 120, "tpb": 480, "tempoMap": tempo, "excerptStartMs": 0, "durationMs": 30000, "notes": notes}
        self.assertEqual([e[0] for e in sp.quantize(s)["events"]], [0, 8, 20])

    def test_unknown_notes_lanes_and_accents(self):
        s = song([(0, 36), (4, 54), (8, 38)])                                 # 54: tambourine, no role
        s["notes"][2]["v"] = 120
        s["notes"].append({"t": 0, "lane": "guitar", "p": 36, "v": 100})
        q = sp.quantize(s)
        self.assertEqual(q["dropped"], 1)
        self.assertEqual([(e[0], e[1], e[2]) for e in q["events"]], [(0, 0, 0), (8, 1, 1)])   # only the loud one is an accent

    def test_two_hits_on_one_step_become_one(self):
        cells = sp.window(sp.quantize(song([(0, 42), (0, 44)]))["events"], 0)
        self.assertEqual(sp.count(cells)[2], 1)


class Window(unittest.TestCase):
    def test_best_bar_takes_the_window_with_the_kit(self):
        h = [(s, 36) for s in range(0, 64, 4)]                                # four bars of kick only
        h += four_bars(base=64 * 2)                                           # then a full kit, from bar 9
        q = sp.quantize(song(h))
        self.assertEqual(sp.best_bar(q["events"]), 8)

    def test_render_has_the_eight_roles(self):
        text = sp.render(sp.window(sp.quantize(song(four_bars()))["events"], 0))
        self.assertEqual([ln.split()[0] for ln in text.splitlines()], sp.ROLES)
        self.assertEqual(len(text.splitlines()[0].split(None, 1)[1]), 64 + 3 + 12)


class Reference(unittest.TestCase):
    def test_the_golden_patterns_are_read(self):
        styles = sp.read_golden(ROOT / "tests/grind/golden")
        self.assertEqual(sorted(styles), list(range(1, 23)))
        self.assertTrue(all(len(v[1]) == 4 for v in styles.values()))
        dens, hist = sp.style_features(styles[1][1])                          # Traditional: kick on every second 16th... of a bar
        self.assertGreater(dens[0], 0)
        self.assertAlmostEqual(sum(hist[0]), 1.0)

    def test_compare_runs_and_names_a_style(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "songs.json"
            p.write_text(json.dumps({"01 Test": song(four_bars())}), encoding="utf-8")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                sp.main(["compare", str(p)])
        self.assertIn("01 Test (120 BPM)", out.getvalue())
        self.assertIn("nearest:", out.getvalue())

    def test_distance_is_zero_for_the_same_features(self):
        f = sp.song_features(sp.window(sp.quantize(song(four_bars()))["events"], 0))
        self.assertEqual(sp.distance(f, f), 0.0)


class Device(unittest.TestCase):
    class Fake:
        def __init__(self):
            self.sent = []

        def req(self, cmd, args, *a, **k):
            self.sent.append((cmd, list(args)))
            return []

    def test_ids_come_from_the_editor_data(self):
        pid, gbpm = sp.ids()
        self.assertEqual(set(pid), {"SLEN", "SDIV", "SSWING", "SRC"})
        self.assertGreater(pid["SDIV"], pid["SLEN"])

    def test_send_writes_timing_then_steps_with_nudge(self):
        cells = sp.window(sp.quantize(song(four_bars()))["events"], 0)
        cells[0][0]["acc"] = 1
        cells[0][8]["nudge"] = -15
        link = self.Fake()
        sp.send(link, cells, bpm=150.4, var=2, save=True, log=lambda *_: None)
        sets = [a for c, a in link.sent if c == sp.SET and a[0] == 0]
        self.assertEqual(len(sets), 8 * 4)                                    # LEN DIV SWG SRC on every track
        pid, gbpm = sp.ids()
        first = sets[0]
        self.assertEqual(first[:2], [0, 0])
        self.assertEqual(first[2:4], sp.u14(pid["SLEN"]))
        self.assertEqual(first[4:], sp.v14(64))
        self.assertEqual(sets[1][4:], sp.v14(2))                              # DIV 2 = 16th
        steps = [a for c, a in link.sent if c == sp.STEPS_SET]
        self.assertEqual(len(steps), 8)
        k = steps[0]
        self.assertEqual(k[:5], [0x7F, 2, 0, 0, 64])
        self.assertEqual(len(k), 5 + 64 * 3 + 64)
        self.assertEqual(k[5:8], [3, 0, 60])                                  # step 1: ON + ACC, C4
        self.assertEqual(k[8:11], [0, 0, 60])                                 # step 2: empty
        self.assertEqual(k[5 + 192 + 8], 64 - 15)                             # step 9's nudge
        self.assertTrue(all(0 <= b <= 127 for _, a in link.sent for b in a))  # 7-bit data only
        self.assertIn((sp.SET, [1, 0] + sp.u14(gbpm) + sp.v14(150)), link.sent)
        self.assertEqual(link.sent[-1], (sp.SAVE, [0x7F]))

    def test_send_without_bpm_or_save(self):
        link = self.Fake()
        sp.send(link, sp.window([], 0), log=lambda *_: None)
        self.assertFalse(any(c in (sp.SAVE,) or (c == sp.SET and a[0] == 1) for c, a in link.sent))


class Files(unittest.TestCase):
    def test_pick_by_number_or_part_of_the_name(self):
        songs = {"01 Alpha": 1, "02 Beta": 2}
        self.assertEqual(sp.pick(songs, "02")[0], "02 Beta")
        self.assertEqual(sp.pick(songs, "alp")[0], "01 Alpha")
        with self.assertRaises(sp.SongError):
            sp.pick(songs, "0")

    def test_a_bad_file_is_an_error_not_a_traceback(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.json"
            p.write_text("[]", encoding="utf-8")
            with self.assertRaises(SystemExit):
                sp.main(["list", str(p)])


if __name__ == "__main__":
    unittest.main()
