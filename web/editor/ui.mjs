// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Small DOM helpers the page's views share (the shell, the header, every tab). Nothing here runs at import time.

/** text for innerHTML */
export const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
/** an element: h("button", { class: "x", onclick }, "text", child, ...) */
export function h(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else if (k === "class") e.className = v;
    else e.setAttribute(k, v === true ? "" : v);
  }
  for (const k of kids.flat()) if (k !== undefined && k !== null && k !== false) e.append(k instanceof Node ? k : String(k));
  return e;
}
export const SLOTS = ["A", "B", "C", "D"];
/** ctx.choose(message, options) -> the option chosen (app.mjs: a dialog with a button each); without it, ctx.confirm:
 * OK = options[1] (Continue), Cancel = options[2] (Cancel) */
export const chooser = (ctx) => ctx.choose || (async (msg, opts) => (await ctx.confirm?.(`${msg}\n\nOK: ${opts[1]}. Cancel: ${opts[2]}.`) ? opts[1] : opts[2]));
