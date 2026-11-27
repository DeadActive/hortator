# SPDX-License-Identifier: GPL-3.0-only
"""Every function that writes the live native pattern must invalidate a staged Grind candidate.

A candidate is made from the pattern as it was; if anything else writes a step (or LEN / DIV / SWG / SRC) before the
cue boundary and does not call gm_touch(), the boundary would overwrite that edit. This is a static audit of
firmware/src: a function that stores into a live step or timing parameter has to contain gm_touch*, or be listed here
with the reason it is safe. A new writer fails the test until somebody decides which it is.
"""
import re
import unittest
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "firmware" / "src"

# the live pattern: the selected track's or any track's steps
LIVE = re.compile(r"(?:\bTSEL|\btrk\[[^\]]*\]|\bt)\s*(?:->|\.)\s*step\b")
# a store into a step, a bulk write of steps, or a project/variation steps load into the live tracks
STORE = re.compile(
    r"->\s*(?:on|acc|slide|tie|rat|cond|nt|micro)\s*(?:=(?!=)|\|=|&=|\^=)"      # s->on = .. (not a local struct's x.on)
    r"|\]\s*\.\s*(?:on|acc|slide|tie|rat|cond|nt|micro)\s*(?:=(?!=)|\|=|&=|\^=)"    # t->step[i].on = ..
    r"|\.step\[[^\]]*\]\s*=(?!=)"
    r"|\bmemset\(\s*t->step\b|\bmem_swap\(\s*t->step\b|\bproj_steps_to\(\s*t->step\b"
)
# a timing parameter written by number
TIMING = re.compile(r"\bp\[\s*(?:P_SLEN|P_SDIV|P_SSWING|P_SRC)\s*\]\s*=(?!=)|\bp\[id\]\s*=(?!=)")
TOUCH = re.compile(r"\bgm_touch(?:_isr|_param|_param_isr)?\s*\(")

# (file, function): why a writer without gm_touch is safe
ALLOWED = {
    ("drum_core.c", "drum_tracks_init"): "power-on / TOOLS INIT reset; its callers touch (init_all) or run before audio (boot)",
    ("ui.c", "drum_boot_init"): "boot, before audio and USB start; the controller starts empty",
    ("bench.c", "bench_kit"): "console-only benchmark on a freshly reset kit, never reachable from the panel",
    ("bench.c", "bench_heavy"): "console-only benchmark pattern, never reachable from the panel",
    ("ui.c", "seq_clear_all"): "calls track_clear, which touches before the first write",
    ("seq.c", "keys_rec"): "its first statement is rec_hit, which touches before any step is written",
    ("seq.c", "midi_play"): "the step index is rec_hit's result, which touches before the write",
}
# files that are not part of the firmware image's live path or only write staging buffers
SKIP_FILES = {"varpack.c"}  # writes the caller's staging buffer (project.c var_get callers pass g->st, never trk)


def functions(text):
    """Yield (name, body) of the top-level functions: a line starting in column 0 ending in ')' followed by '{'."""
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        if lines[i].startswith("{") and i > 0:
            j = i - 1
            while j > 0 and not re.search(r"\w+\s*\(", lines[j]):
                j -= 1
            m = re.search(r"(\w+)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*(?:/\*.*)?$", lines[j])
            name = m.group(1) if m else lines[j].strip()
            k = i
            while k < len(lines) and lines[k] != "}":
                k += 1
            yield name, "\n".join(lines[i:k + 1])
            i = k
        i += 1


class EditSiteAudit(unittest.TestCase):
    def test_every_live_pattern_writer_touches(self):
        offenders = []
        for path in sorted(SRC.glob("*.c")):
            if path.name in SKIP_FILES:
                continue
            text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
            for name, body in functions(text):
                writes = (LIVE.search(body) and STORE.search(body)) or (
                    TIMING.search(body) and re.search(r"\bTSEL\b|\bt\s*->\s*p\b|trk\[", body)
                    and re.search(r"P_SLEN|P_SDIV|P_SSWING|P_SRC|\bid\b", body))
                if writes and not TOUCH.search(body) and (path.name, name) not in ALLOWED:
                    offenders.append(f"{path.name}:{name}")
        self.assertEqual([], offenders, "writes the live pattern without gm_touch(): add it, or list it in ALLOWED")

    def test_page_knobs_touch_timing_parameters(self):
        # the generic page knob writes through a pointer, which cannot be classified statically: its hook is checked by
        # name instead (LEN DIV SWG SRC are pattern data); HOME's knobs are sound parameters and never timing
        text = (SRC / "ui_input.c").read_text(encoding="utf-8", errors="replace")
        self.assertIn("gm_touch_param((uint32_t)(vp - TSEL->p))", text)
        self.assertIn("gm_touch_param(id);", text)                      # the LEN slot
        for fname in ("editor.c", "midi_learn.c"):
            self.assertIn("gm_touch_param_isr(id)", (SRC / fname).read_text(encoding="utf-8", errors="replace"))

    def test_allowed_entries_still_exist(self):
        for (fname, func), why in ALLOWED.items():
            text = (SRC / fname).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
            self.assertTrue(any(n == func for n, _ in functions(text)), f"{fname}:{func} is gone; drop it ({why})")

    def test_touch_variant_matches_the_irq_state(self):
        # gm_touch() disables and re-enables the IRQ itself: inside an fm1_irq_off() region it would re-enable it early
        bad = []
        for path in sorted(SRC.glob("*.c")):
            text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
            for name, body in functions(text):
                depth_off = False
                for line in body.split("\n"):
                    if "fm1_irq_off()" in line:
                        depth_off = True
                    elif "fm1_irq_on()" in line:
                        depth_off = False
                    elif depth_off and re.search(r"\bgm_touch(?:_param)?\s*\(", line):
                        bad.append(f"{path.name}:{name}")
        self.assertEqual([], bad, "gm_touch()/gm_touch_param() inside an IRQ-off region: use the _isr variant")


if __name__ == "__main__":
    unittest.main()
