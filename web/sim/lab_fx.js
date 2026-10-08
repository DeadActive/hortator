// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 7, effects per track and live: a groove whose rimshot runs through its track's chain (FILTER, RESON, DIST,
// SLICER, then the delay, reverb and chorus sends), each stage switchable with one knob; stopped, the firmware
// renders a bar of it. Playing, the picture is the output as heard, scrolling, and the PERFORM keys (the black keys
// with FX held, perform.c PF_BLACK) repeat, reverse, stop, filter, freeze or shift the whole mix while held.
import { el, knob, scope, INK } from './lab_ui.js';
import * as draw from './lab_draw.js';

const KEYS = [                                       // perform.c PF_BLACK: the black keys from F#3, in groups of 3 and 2
  ['R8', 'REPEAT 1/8'], ['R16', 'REPEAT 1/16'], ['R32', 'REPEAT 1/32'], ['REV', 'REVERSE'], ['TAPE', 'TAPE STOP'],
  ['LPF', 'LPF'], ['HPF', 'HPF'], ['FRZ', 'FREEZE'], ['OUP', 'OCT UP'], ['ODN', 'OCT DN'],
];
const GROUPS = [3, 2, 3, 2];
const RIM = [0, 3, 6, 10, 11, 14];                   // the rimshot's figure

export default function fx(plate, { lab, ids, audition }) {
  const { fs, P, DM, RS, tp, names } = ids;
  const ft = n => names.ftype.indexOf(n), sl = n => tp.SLCR.names.indexOf(n);
  const STAGES = [                                   // [name, on, the knob's descriptor, value, its parameters on / off]
    { name: 'FILTER', on: true, d: tp.FCUT, v: 58,
      set: (on, v) => [[P.FTYPE, on ? ft('LP') : 0], [P.FCUT, v], [P.FRESO, 70], [P.FENV, 40]] },
    { name: 'RESON', on: true, d: tp.RDECAY, v: 90,
      set: (on, v) => [[P.RMODEL, on ? RS.CHORD : RS.OFF], [P.RDECAY, v], [P.RMIX, 110], [P.RTUNE, 57]] },
    { name: 'DIST', on: false, d: tp.DIST, v: 70, set: (on, v) => [[P.DIST, on ? v : 0]] },
    { name: 'SLICER', on: false, d: tp.SLPAT, v: 5,
      set: (on, v) => [[P.SLCR, on ? sl('GATE') : 0], [P.SLPAT, v], [P.SLRATE, 1], [P.SLDEPTH, 127]] },
    { name: 'DELAY', send: true, on: true, d: tp.DLY, v: 50, set: (on, v) => [[P.DLY, on ? v : 0]] },
    { name: 'REVERB', send: true, on: false, d: tp.REV, v: 60, set: (on, v) => [[P.REV, on ? v : 0]] },
    { name: 'CHORUS', send: true, on: false, d: tp.CHOR, v: 70, set: (on, v) => [[P.CHOR, on ? v : 0]] },
  ];
  const bar = fs * 2;
  let player = null, buf = new Float32Array(0), queued = false, tape = null, raf = 0, looping = false;

  const kit = t => {
    t.call('model', 0, DM.K909);
    t.call('model', 1, DM.RIM);
    t.call('model', 2, DM.HATC);
    for (let s = 0; s < 16; s++) {
      t.call('step', 0, s, s % 4 === 0, false);
      t.call('step', 1, s, RIM.includes(s), s === 0);
      t.call('step', 2, s, s % 2 === 0, false);
    }
    t.call('param', 2, P.LEVEL, 70);
    chain(t);
  };
  const chain = t => { for (const st of STAGES) for (const [p, v] of st.set(st.on, st.v)) t.call('param', 1, p, v); };
  const local = { call: (fn, ...a) => lab[fn](...a) };

  const render = () => {                             // stopped: a bar after a bar (the tails and echoes in)
    queued = false;
    lab.reset();
    kit(local);
    lab.play(true);
    lab.renderMono(bar);
    buf = lab.renderMono(bar);
    waveScope.redraw();
  };
  const queue = () => { if (!queued) { queued = true; requestAnimationFrame(render); } };
  const changed = () => {
    if (player) chain(player);
    paintChain();
    queue();
  };

  // ---- the picture: a bar, or (playing) the output as heard, the last 2 s scrolling (peaks per 1/200 s)
  const COLS = 400;
  const waveScope = scope('wave', (g, w, h) => {
    if (tape) {
      const mid = h / 2;
      g.save();
      g.fillStyle = INK.led;
      g.shadowColor = INK.glow;
      g.shadowBlur = 8;
      const cw = w / COLS, top = Math.max(0.05, ...tape.peaks);   // drawn to the window's loudest
      for (let i = 0; i < COLS; i++) {
        const v = tape.peaks[(tape.at + i) % COLS] / top;
        g.fillRect(i * cw, mid - v * mid * 0.95, Math.max(1, cw - 0.5), Math.max(1, v * mid * 1.9));
      }
      g.restore();
      g.save();
      g.font = '11px "IBM Plex Sans", system-ui, sans-serif';
      g.fillStyle = INK.dim;
      g.textBaseline = 'top';
      g.fillText('live, the last 2 s', 2, 2);
      g.restore();
      return;
    }
    let pk = 0;
    for (let i = 0; i < buf.length; i++) pk = Math.max(pk, Math.abs(buf[i]));
    draw.wave(g, w, h - 16, buf, bar, { scale: 0.92 / (pk || 1) });
    draw.timeAxis(g, w, h, bar / fs, 'one bar, as the firmware renders it');
  });
  const tick = () => {                               // the analyser's newest samples -> peaks of 1/200 s columns
    raf = requestAnimationFrame(tick);
    const an = player?.analyser;
    if (!an || !tape) return;
    const now = an.context.currentTime, a = tape.data;
    an.getFloatTimeDomainData(a);
    const per = an.context.sampleRate / 200;
    const elapsed = Math.round((now - tape.t) * an.context.sampleRate), fresh = Math.min(a.length, elapsed);
    tape.t = now;
    for (let gap = elapsed - fresh; gap >= per; gap -= per) {   // a long frame: the samples it missed, as a gap
      tape.peaks[tape.at] = 0;
      tape.at = (tape.at + 1) % COLS;
    }
    tape.carry += fresh;
    let i = a.length - fresh;
    while (tape.carry >= per) {
      let p = 0;
      for (const end = Math.min(a.length, i + per); i < end; i++) p = Math.max(p, Math.abs(a[i]));
      tape.peaks[tape.at] = p;
      tape.at = (tape.at + 1) % COLS;
      tape.carry -= per;
    }
    waveScope.redraw();
  };

  // ---- the chain: the voice, the inserts in their order, then the sends
  const chainEl = el('div', 'chain');
  const blocks = STAGES.map(st => {
    const b = el('div', `stage${st.send ? ' send' : ''}`), name = el('button', 'stage-name', st.name);
    name.type = 'button';
    name.addEventListener('click', () => { st.on = !st.on; changed(); });
    const k = knob({ ...st.d, label: st.d.label }, v => { st.v = v; if (st.on) changed(); }, { value: st.v });
    b.append(name, k.root);
    return b;
  });
  const voice = el('div', 'stage voice');
  voice.append(el('span', 'stage-name fixed', 'RIM'));
  const sends = el('div', 'sends');
  sends.append(...blocks.slice(4));
  chainEl.append(voice, ...blocks.slice(0, 4), sends);
  const paintChain = () => STAGES.forEach((st, i) => {
    blocks[i].classList.toggle('on', st.on);
    blocks[i].querySelector('.stage-name').setAttribute('aria-pressed', String(st.on));
  });

  // ---- PERFORM: held keys
  const keysEl = el('div', 'perform-keys');
  const held = new Set();
  const press = async (e, btn, down) => {
    if (down === held.has(e)) return;
    if (down) held.add(e); else held.delete(e);
    btn.classList.toggle('down', down);
    const p = down ? await startLoop() : player;
    p?.call('perf', ids.PF[e], down);
  };
  let k = 0;
  for (const n of GROUPS) {
    const group = el('div', 'key-group');
    for (let i = 0; i < n; i++, k++) {
      const [e, label] = KEYS[k], btn = el('button', 'perf-key', label);
      btn.type = 'button';
      btn.addEventListener('pointerdown', ev => { btn.setPointerCapture(ev.pointerId); ev.preventDefault(); press(e, btn, true); });
      for (const t of ['pointerup', 'pointercancel', 'lostpointercapture']) btn.addEventListener(t, () => press(e, btn, false));
      btn.addEventListener('keydown', ev => { if ((ev.key === ' ' || ev.key === 'Enter') && !ev.repeat) { ev.preventDefault(); press(e, btn, true); } });
      btn.addEventListener('keyup', ev => { if (ev.key === ' ' || ev.key === 'Enter') press(e, btn, false); });
      group.append(btn);
    }
    keysEl.append(group);
  }
  const startLoop = async () => {
    const p = await audition();
    if (p && !looping) {
      p.call('play', true);
      looping = true;
    }
    return p;
  };

  const controls = el('div', 'controls-row');
  const perf = el('div', 'perform');
  perf.append(el('p', 'family-name', 'PERFORM: hold a key'), keysEl);
  controls.append(perf, el('div', 'play-slot'));
  plate.append(chainEl, waveScope.canvas, controls);
  paintChain();
  render();

  return {
    start(p, loop) {
      player = p;
      looping = loop;
      kit(p);
      tape = { data: new Float32Array(p.analyser.fftSize), peaks: new Float32Array(COLS), at: 0, carry: 0,
        t: p.ctx.currentTime };
      cancelAnimationFrame(raf);
      tick();
      if (loop) p.call('play', true);
    },
    stop() {
      for (const e of held) player?.call('perf', ids.PF[e], false);
      held.clear();
      for (const b of keysEl.querySelectorAll('.down')) b.classList.remove('down');
      cancelAnimationFrame(raf);
      player = null;
      looping = false;
      tape = null;
      waveScope.redraw();
    },
  };
}
