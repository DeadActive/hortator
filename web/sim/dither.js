// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The page's look, from the FM-1's screen: tones as the firmware's palette (MONO: five greys and white), blends as
// ordered dithering (Bayer 4 x 4, 2 px cells, like the LCD seen up close), and headings that glitch once as they arrive.

const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
const rgb = hex => [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16));

// a field (x, y in 0..1 -> 0..1) as two tones, dithered; cell: CSS pixels per dither pixel
export function ditherCanvas(w, h, field, from, to, cell = 2) {
  const cw = Math.max(1, Math.ceil(w / cell)), ch = Math.max(1, Math.ceil(h / cell));
  const c = document.createElement('canvas');
  c.width = cw;
  c.height = ch;
  const g = c.getContext('2d'), img = g.createImageData(cw, ch), px = img.data, a = rgb(from), b = rgb(to);
  for (let y = 0; y < ch; y++)
    for (let x = 0; x < cw; x++) {
      const v = field((x + 0.5) / cw, (y + 0.5) / ch), t = (BAYER[(y & 3) * 4 + (x & 3)] + 0.5) / 16, k = v > t ? b : a, o = 4 * (y * cw + x);
      px[o] = k[0]; px[o + 1] = k[1]; px[o + 2] = k[2]; px[o + 3] = 255;
    }
  g.putImageData(img, 0, 0);
  return c;
}

// an element's background as a dithered field, redrawn as it resizes
export function ditherBackground(el, field, from, to, cell = 2) {
  if (!el) return;
  let last = '';
  new ResizeObserver(([e]) => {
    const w = Math.round(e.contentRect.width), h = Math.round(e.contentRect.height), key = `${w}x${h}`;
    if (!w || !h || key === last) return;
    last = key;
    const c = ditherCanvas(w, h, field, from, to, cell);
    el.style.backgroundImage = `url(${c.toDataURL()})`;
    el.style.backgroundSize = `${c.width * cell}px ${c.height * cell}px`;
  }).observe(el);
}

// a canvas fill pattern: the tone at a density (0..1) over black, dithered, for the pieces' areas
const patterns = new Map();
export function ditherPattern(g, tone, density) {
  const key = `${tone} ${density}`;
  if (!patterns.has(key)) patterns.set(key, ditherCanvas(8, 8, () => density, '#000000', tone, 2));
  return g.createPattern(patterns.get(key), 'repeat');
}

// headings glitch: once as each comes into view; the hero's title now and then
export function initGlitch(root = document) {
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  const run = el => {
    el.classList.remove('glitching');
    void el.offsetWidth;                              // restart the animation
    el.classList.add('glitching');
  };
  const heads = [...root.querySelectorAll('.hero-copy h1, .features-head h2, .ch-text h3, .install-head h2, .foot-mark')];
  for (const el of heads) {
    el.dataset.text = el.textContent;
    el.classList.add('glitch');
    el.addEventListener('animationend', e => { if (e.animationName === 'glitch-base') el.classList.remove('glitching'); });
  }
  const io = new IntersectionObserver(entries => {
    for (const e of entries) if (e.isIntersecting) { run(e.target); io.unobserve(e.target); }
  }, { threshold: 0.6 });
  heads.forEach(el => io.observe(el));
  const title = root.querySelector('.hero-copy h1');
  if (title) {
    const again = () => setTimeout(() => {
      if (!document.hidden && title.getBoundingClientRect().bottom > 0) run(title);
      again();
    }, 5000 + Math.random() * 5000);
    again();
  }
}
