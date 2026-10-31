// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The header bar (always shown while connected): the FM-1 and its firmware, PLAY / STOP, the slots (loaded, unsaved
// "A*", queued), the variation, RESTORING / the update silence, and the eight track keys: one selection, the device's.
import { NONE, SF } from "./proto.mjs";
import { ASLEEP_MSG, BUSY_MSG } from "./session.mjs";
import { SLOTS, esc } from "./ui.mjs";

/** what the header shows, from the session (pure: the tests read it) */
export function headerModel(s) {
  const st = s.state || {}, info = s.info || {};
  const slots = SLOTS.map((n, i) => ({ name: n, used: !!(st.used >> i & 1), dirty: !!(st.dirty >> i & 1),
    loaded: st.slot === i, queued: st.qslot === i, text: n + (st.dirty >> i & 1 ? "*" : "") }));
  const status = s.asleep ? ASLEEP_MSG : s.busy ? BUSY_MSG : st.flags & SF.RESTORING ? "RESTORING" : st.flags & SF.SONG ? "SONG" : "";
  return {
    device: "HORTATOR FM-1", port: s.port?.name || "", firmware: (info.firmware || "") + (info.ble ? " BT" : ""),
    play: st.playing ? "PLAY" : "STOP", playing: !!st.playing,
    slot: st.slot === NONE || st.slot === undefined ? "KIT" : SLOTS[st.slot] + (st.dirty >> st.slot & 1 ? "*" : ""),
    variation: st.var === undefined ? "" : `VAR ${st.var + 1}`,
    queued: [st.qslot !== undefined && st.qslot !== NONE ? SLOTS[st.qslot] : "", st.qvar !== undefined && st.qvar !== NONE ? `VAR ${st.qvar + 1}` : ""]
      .filter(Boolean).join(" "),
    slots, sel: st.sel ?? 0, status, restoring: !!(st.flags & SF.RESTORING), busy: !!s.busy, asleep: !!s.asleep,
  };
}

export function mountHeader(el, s, { onError } = {}) {
  el.innerHTML = `<div class="hb-dev"><span class="silk">FM-1</span> <span class="hb-fw"></span></div>
    <div class="hb-play" aria-live="polite"></div><div class="hb-slots" aria-label="Slots"></div>
    <div class="hb-var"></div><div class="hb-status" role="status"></div>
    <button type="button" class="hb-re" hidden>Reconnect</button>
    <div class="hb-tracks" role="group" aria-label="Track">${[1, 2, 3, 4, 5, 6, 7, 8].map((n) =>
    `<button type="button" class="trk" data-t="${n - 1}" aria-pressed="false">${n}</button>`).join("")}</div>`;
  const q = (c) => el.querySelector(c);
  el.querySelector(".hb-tracks").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-t]");
    if (b) s.select(Number(b.dataset.t)).catch((err) => onError?.(err.message));
  });
  q(".hb-re").addEventListener("click", () => s.reconnect());
  const draw = () => {
    const m = headerModel(s);
    q(".hb-fw").textContent = `${m.firmware} · ${m.port}`;
    q(".hb-play").textContent = m.play;
    q(".hb-play").classList.toggle("on", m.playing);
    q(".hb-slots").innerHTML = m.slots.map((x) => `<span class="sl${x.loaded ? " on" : ""}${x.used ? "" : " empty"}${x.queued ? " q" : ""}"
      title="${x.used ? "" : "empty, "}${x.dirty ? "unsaved changes" : "saved"}">${esc(x.text)}</span>`).join("");
    q(".hb-var").textContent = m.variation + (m.queued ? ` → ${m.queued}` : "");
    q(".hb-status").textContent = m.status;
    q(".hb-status").classList.toggle("alert", m.busy || m.restoring);
    q(".hb-re").hidden = !m.busy; // asleep: the only way to ask again; busy: ask now instead of waiting
    for (const b of el.querySelectorAll("button.trk")) {
      const on = Number(b.dataset.t) === m.sel;
      b.classList.toggle("on", on);
      b.setAttribute("aria-pressed", on ? "true" : "false");
      b.disabled = m.busy;
    }
  };
  const offs = [s.on("state", draw), s.on("busy", draw), s.on("asleep", draw)];
  draw();
  return { draw, destroy: () => offs.forEach((f) => f()) };
}
