// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The Patterns tab: a placeholder until its view lands. Steps per variation, locks, cue, copy variation (task 8).
// A tab module exports `tab = { id, title, cap, mount(el, ctx) }`: app.mjs shows the tab when INFO's caps have `cap`,
// mounts it once into its panel after connecting, and calls the returned { show?, destroy? }. ctx: { session (session.mjs:
// request / on("state" | "changed" | "steps" | "locks" | "busy" | "resync") / state / info), toast(msg), confirm(msg) }.
import { CAP } from "./proto.mjs";

export function mountPatterns(el) {
  el.innerHTML = '<p class="placeholder">Patterns: not in this build of the editor yet.</p>';
  return {};
}

export const tab = { id: "patterns", title: "Patterns", cap: CAP.PATTERNS, mount: mountPatterns };
