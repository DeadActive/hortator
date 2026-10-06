// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The landing page's reel (tests/sim_record.c): the firmware's screen, LEDs and knob turns recorded at a fixed rate,
// replayed on the panel until the real firmware is switched on. Format "FM1R" v1, little-endian: header "FM1R",
// u16 version, u16 fps, u32 frames; per frame u32 button LEDs, u32 key LEDs, i8 x 8 encoder steps, u32 runs, then runs
// of (u16 start, u16 length, RGB565 pixels). Frame 0 is the whole screen; the others only what changed.
export function parseReel(buf) {
  const v = new DataView(buf);
  const magic = String.fromCharCode(v.getUint8(0), v.getUint8(1), v.getUint8(2), v.getUint8(3));
  if (magic !== 'FM1R' || v.getUint16(4, true) !== 1) throw new Error('not an FM1R v1 reel');
  const fps = v.getUint16(6, true), frames = v.getUint32(8, true), at = [];
  let o = 12;
  for (let i = 0; i < frames; i++) {                    // index the frames once
    at.push(o);
    const runs = v.getUint32(o + 16, true);
    o += 20;
    for (let r = 0; r < runs; r++) o += 4 + 2 * v.getUint16(o + 2, true);
  }
  if (o !== buf.byteLength) throw new Error('reel: trailing or missing bytes');
  return {
    fps,
    frames,
    // frame i's changes onto fb (Uint16Array 240 x 240) -> { leds, keyLeds, enc }
    apply(i, fb) {
      let p = at[i];
      const leds = v.getUint32(p, true), keyLeds = v.getUint32(p + 4, true);
      const enc = Array.from({ length: 8 }, (_, k) => v.getInt8(p + 8 + k));
      const runs = v.getUint32(p + 16, true);
      p += 20;
      for (let r = 0; r < runs; r++) {
        const start = v.getUint16(p, true), len = v.getUint16(p + 2, true);
        p += 4;
        for (let k = 0; k < len; k++, p += 2) fb[start + k] = v.getUint16(p, true);
      }
      return { leds, keyLeds, enc };
    },
  };
}
