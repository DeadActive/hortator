// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 5, the sequencer's chance and live hands: a hi-hat lane over a kick and a snare. Each step has its PROB (a
// chance, 1-SHOT, or A of every B bars) and RATCH; the firmware plays the lane for 8 bars in the analyser and the dice
// view shows what each bar played: the same 8 bars after every PLAY, as on the FM-1 (seq.c seeds the dice at PLAY).
// Playing: the steps light as they fire; the hats mute at once or on the next bar (waiting shown); TONE turns record
// into the pattern (MOTION) and play back.
import { el, knob, chips, scope, INK } from './lab_ui.js?v=7bf91331b2';

const BARS = 8;
const PATTERN = ['100%', '30%', '100%', '60%', '100%', '30%', '1/2', '60%',   // the hats' PROB per step
  '100%', '30%', '100%', '60%', '100%', '1-SHOT', '100%', '50%'];

export default function seq(plate, { lab, ids, audition, screen }) {
  const { fs, P, DM, conds, models } = ids;
  const posOf = name => conds.findIndex(c => c.name === name);
  const steps = PATTERN.map((n, s) => ({ on: true, pos: posOf(n), rat: s === 14 ? 3 : 0 }));
  const tone = models[DM.HATC].edit[2];
  const live = { atBar: true, rec: false, tone: tone.def };
  let sel = 1, player = null, dice = [], queued = false, playing = false;
  let now = -1, lastIdx = -1, muteQ = 0, muted = 0, motionVals = new Array(16).fill(null);

  const kit = t => {
    t.call('model', 0, DM.K909);
    t.call('model', 1, DM.HATC);
    t.call('model', 2, DM.S909);
    for (let s = 0; s < 16; s++) {
      t.call('step', 0, s, s % 4 === 0, false);
      t.call('step', 2, s, s === 4 || s === 12, false);
    }
    lane(t);
  };
  const lane = t => steps.forEach((st, s) => t.call('step', 1, s, st.on, s % 4 === 0, conds[st.pos].store, st.rat));
  const local = { call: (fn, ...a) => lab[fn](...a) };

  const render = () => {                             // 8 bars from PLAY: what fired on each step of the hats
    queued = false;
    lab.reset();
    kit(local);
    lab.play(true);
    dice = lab.dice(1, BARS);
    diceScope.redraw();
    screen();
  };
  const queue = () => { if (!queued) { queued = true; requestAnimationFrame(render); } };

  // ---- the lane: a pad per step (on / off) and its PROB / RATCH under it, picked for the knobs
  const laneEl = el('div', 'lane');
  const cells = steps.map((st, s) => {
    const col = el('div', 'lane-step'), pad = el('button', 'step-pad'), tag = el('button', 'step-tag');
    pad.type = tag.type = 'button';
    pad.setAttribute('aria-label', `Step ${s + 1}`);
    tag.setAttribute('aria-label', `Step ${s + 1}: chance and ratchet`);
    pad.addEventListener('click', () => {
      st.on = !st.on;
      if (st.on) pick(s);
      edited();
    });
    tag.addEventListener('click', () => pick(s));
    col.append(pad, tag);
    laneEl.append(col);
    return { col, pad, tag };
  });
  const paint = () => cells.forEach(({ col, pad, tag }, s) => {
    const st = steps[s];
    pad.setAttribute('aria-pressed', String(st.on));
    const name = conds[st.pos].name.replace(/^(.+)-(.{3,})$/, '$1-\n$2');   // 8 px a letter: 1-SHOT on two lines
    tag.textContent = st.on ? name + (st.rat ? `\n\u00d7${st.rat + 1}` : '') : '';   // RATCH: a line of its own
    tag.disabled = !st.on;
    tag.setAttribute('aria-pressed', String(s === sel));
    col.classList.toggle('now', s === now);
    col.classList.toggle('beat', s % 4 === 0);
  });
  const edited = () => {
    if (player) lane(player);
    paint();
    queue();
  };

  // ---- the step's knobs (on the FM-1: hold the step, turn KNOB1 / KNOB2)
  const selName = el('p', 'sel-name');
  const condKnob = knob({ label: 'PROB', min: 0, max: conds.length - 1, def: 20 }, v => { steps[sel].pos = v; edited(); },
    { value: steps[sel].pos, show: v => conds[v].name });
  const ratKnob = knob({ label: 'RATCH', min: 0, max: 3, def: 0 }, v => { steps[sel].rat = v; edited(); },
    { value: steps[sel].rat, show: v => String(v + 1) });
  function pick(s) {
    sel = s;
    selName.textContent = `Step ${s + 1}`;
    condKnob.set(steps[s].pos);
    ratKnob.set(steps[s].rat);
    paint();
  }

  // ---- the dice: 8 bars from PLAY, what each step played (lit; ratchets as slivers), skipped (hollow), off (dot)
  const diceScope = scope('dice', (g, w, h) => {
    const left = 30, cw = (w - left) / 16, rh = h / BARS;
    g.save();
    g.font = '16px "Hortator Terminus", monospace';
    g.textBaseline = 'middle';
    for (let b = 0; b < BARS; b++) {
      g.fillStyle = INK.dim;
      g.fillText(String(b + 1), 4, b * rh + rh / 2);
      for (let s = 0; s < 16; s++) {
        const x = left + s * cw + 3, y = b * rh + 3, cwi = cw - 6, chi = rh - 6, n = dice[b]?.[s] ?? 0;
        if (!steps[s].on) {
          g.fillStyle = INK.grid;
          g.fillRect(x + cwi / 2 - 1.5, y + chi / 2 - 1.5, 3, 3);
        } else if (!n) {
          g.strokeStyle = '#505050';
          g.strokeRect(x + 0.5, y + 0.5, cwi - 1, chi - 1);
        } else {
          g.fillStyle = INK.led;
          g.shadowColor = INK.glow;
          g.shadowBlur = 6;
          const sw = (cwi - (n - 1) * 2) / n;
          for (let k = 0; k < n; k++) g.fillRect(x + k * (sw + 2), y, sw, chi);
          g.shadowBlur = 0;
        }
      }
    }
    g.fillStyle = 'rgba(255,255,255,.08)';            // the picked step's column
    g.fillRect(left + sel * cw, 0, cw, h);
    g.restore();
  });

  // ---- live: the mute, MOTION
  const startLoop = async () => {
    const p = await audition();
    if (p && !playing) p.call('play', true);
    return p;
  };
  const muteBtn = el('button', 'live-btn', 'Mute the hats');
  muteBtn.type = 'button';
  muteBtn.addEventListener('click', async () => (await startLoop())?.call('mute', 1, live.atBar));
  const muteWhen = chips([{ label: 'NEXT BAR', value: true }, { label: 'AT ONCE', value: false }], live.atBar,
    v => { live.atBar = v; }, 'Mute');
  const muteState = el('span', 'live-state');
  const recBtn = el('button', 'live-btn rec', 'Record knob moves');
  recBtn.type = 'button';
  recBtn.setAttribute('aria-pressed', 'false');
  recBtn.addEventListener('click', async () => {
    live.rec = !live.rec;
    recBtn.setAttribute('aria-pressed', String(live.rec));
    (await startLoop())?.call('motion', 1, live.rec);
  });
  const toneKnob = knob(tone, v => { live.tone = v; player?.call('knob', 1, P.E2, v); }, { value: live.tone });
  const clearBtn = el('button', 'live-btn quiet', 'Clear');
  clearBtn.type = 'button';
  clearBtn.addEventListener('click', () => {
    player?.call('motionClear', 1);
    motionVals.fill(null);
    motionScope.redraw();
  });
  const motionScope = scope('motion', (g, w, h) => {   // the hats' TONE as each step played it (the probes)
    const cw = w / 16;
    g.save();
    g.fillStyle = INK.grid;
    g.fillRect(0, 0, w, h);
    g.fillStyle = INK.warm;
    g.shadowColor = 'rgba(0,0,0,0)';
    g.shadowBlur = 6;
    motionVals.forEach((v, s) => {
      if (v == null) return;
      const bh = Math.max(2, (v - tone.min) / (tone.max - tone.min) * (h - 4));
      g.fillRect(s * cw + 2, h - 2 - bh, cw - 4, bh);
    });
    g.restore();
  });

  const muteGroup = el('div', 'live-group'), motionGroup = el('div', 'live-group');
  muteGroup.append(el('p', 'family-name', 'Mute'), muteWhen.root, muteBtn, muteState);
  const motionRow = el('div', 'motion-row');
  motionRow.append(toneKnob.root, motionScope.canvas);
  motionGroup.append(el('p', 'family-name', 'Motion: the hats’ TONE per step'), motionRow, el('div', 'motion-btns'));
  motionGroup.lastChild.append(recBtn, clearBtn);

  const editRow = el('div', 'controls-row'), stepKnobs = el('div', 'knobs');
  stepKnobs.append(selName, condKnob.root, ratKnob.root);
  editRow.append(stepKnobs, el('div', 'play-slot'));
  const caption = el('p', 'legend dice-caption', 'The first 8 bars after PLAY, the hats only: lit played (slivers: a ratchet), hollow skipped. The FM-1 deals the same 8 after every PLAY.');
  const liveRow = el('div', 'live-row');
  liveRow.append(muteGroup, motionGroup);
  plate.append(laneEl, editRow, diceScope.canvas, caption, liveRow);
  pick(sel);
  render();

  const blink = () => {
    const w = (muteQ >> 1) & 1, m = (muted >> 1) & 1;
    muteState.textContent = w ? (m ? 'unmutes on the bar' : 'mutes on the bar') : m ? 'muted' : '';
    muteState.classList.toggle('waiting', !!w);
    muteBtn.textContent = m ? 'Unmute the hats' : 'Mute the hats';
  };
  return {
    start(p, loop) {
      player = p;
      kit(p);
      p.call('watch', 1, P.E2);
      p.call('knob', 1, P.E2, live.tone);
      if (live.rec) p.call('motion', 1, true);
      p.onProbes(list => {
        for (const q of list) {
          const i = q.idx[1] % 16;
          if (q.fired & 2) cells[i].pad.classList.add('fired');
          if (i !== lastIdx) {
            cells[(i + 15) % 16].pad.classList.remove('fired');
            lastIdx = i;
          }
          motionVals[i] = q.watch;
          muteQ = q.muteQ;
          muted = q.muted;
        }
        now = lastIdx;
        playing = true;
        paint();
        blink();
        motionScope.redraw();
      });
      if (loop) p.call('play', true);
    },
    stop() {
      player = null;
      playing = false;
      now = lastIdx = -1;
      muteQ = muted = 0;
      for (const c of cells) c.pad.classList.remove('fired');
      paint();
      blink();
    },
  };
}
