// SPDX-License-Identifier: GPL-3.0-only
import { CAP, CMD, GRIND, GRIND_MAX, SF } from "./proto.mjs";

const STYLES = [
  "Traditional", "Hammer", "Bomb", "Gravity", "Grind Blast", "D-Beat", "Skank", "Doublebass",
  "Technical", "Brutal", "Fast-Slow", "Abrupt Break", "Stop-and-Go", "Breakdown", "Two-Step",
  "Half-Time", "Chaos", "Industrial", "Polymetric", "Sludge", "Funeral", "Stoner", "Mathcore 7/4",
];
const ROLES = ["Kick", "Snare", "Hi-hat", "Ride", "Crash", "China", "High tom", "Low tom"];

function mount(panel, { session, confirm }) {
  panel.innerHTML = `
    <h2>GRIND</h2>
    <p>Generate a pattern across eight tracks. Stop playback and use <strong>Patterns → Save on FM-1</strong> to save the loaded project. If no project is loaded, choose a slot with <strong>SAVE</strong> on the device.</p>
    <fieldset data-controls><legend>Generator</legend><div class="grind-controls"></div>
      <div class="grind-actions"><button type="button" data-roll>New variation</button>
        <button type="button" data-kit>Apply role kit</button><button type="button" data-undo>Undo</button></div>
    </fieldset>
    <fieldset data-keep><legend>Keep steps</legend>
      <p>Protected hits and rests carry over to the next generation. Changing protection can cancel a waiting pattern.</p>
      <label>Track <select data-track></select></label>
      <div class="grind-actions"><button type="button" data-protect>Keep track</button>
        <button type="button" data-clear>Free track</button><button type="button" data-free>Free all tracks</button></div>
      <div class="grind-steps" data-steps></div>
    </fieldset>
    <p data-status role="status"></p>`;
  const get = (s) => panel.querySelector(s);
  const button = (id, handle) => {
    const node = get(`[data-${id}]`);
    node.addEventListener("click", handle);
    return node;
  };
  const setOptions = (control, options) => options.forEach((text, value) => control.add(new Option(text, value)));
  let state, active = false, writing = false, reading = false, timer, track = 0, feedback = "", waiting;
  const selectTrack = get("[data-track]");
  setOptions(selectTrack, ROLES.map((role, i) => `T${i + 1} · ${role}`));
  selectTrack.addEventListener("change", () => {
    track = Number(selectTrack.value);
    state = undefined;
    feedback = "";
    render();
    refresh();
  });

  const specs = [
    ["Style", STYLES], ["Aggression (%)"], ["Density (%)"], ["Chaos (%)"],
    ["Humanize (%)"], ["Mutation"], ["Apply at", ["Next step", "Pattern end"]],
  ];
  const controls = specs.map(([label, options], index) => {
    const wrapper = document.createElement("label");
    const input = document.createElement(options ? "select" : "input");
    wrapper.textContent = label;
    if (options) setOptions(input, options);
    else {
      input.type = "number";
      input.min = 0;
      input.max = GRIND_MAX[index];
      input.step = 1;
    }
    input.addEventListener("change", () => {
      const value = input.value === "" ? NaN : Number(input.value);
      if (!Number.isInteger(value) || value < 0 || value > GRIND_MAX[index]) {
        input.value = state.knobs[index];
        return;
      }
      send(GRIND.KNOB, index, value);
    });
    wrapper.append(input);
    get(".grind-controls").append(wrapper);
    return input;
  });
  const roll = button("roll", () => send(GRIND.KNOB, 5, (state.knobs[5] + 1) % 100));
  const kit = button("kit", () => {
    if (confirm("Replace the sound models and sound settings of all eight tracks with the GRIND role kit?"))
      send(GRIND.KIT, 0, 0, "Role kit applied.");
  });
  const undo = button("undo", () => {
    if (state.redo || confirm("Restore the pattern before the last GRIND change? Later step edits may be replaced."))
      send(GRIND.UNDO, 0, 0, state.redo ? "GRIND change restored." : "GRIND change undone.");
  });
  const protect = button("protect", () => send(GRIND.KEEP_TRACK, 0, 1));
  const clear = button("clear", () => send(GRIND.KEEP_TRACK, 0, 0));
  const free = button("free", () => send(GRIND.FREE));
  const steps = Array.from({ length: 64 }, (_, index) => {
    const node = document.createElement("button");
    node.type = "button";
    node.textContent = index + 1;
    node.setAttribute("aria-label", `Keep step ${index + 1}`);
    node.addEventListener("click", () => send(GRIND.KEEP_STEP, index, Number(!state.kept[index])));
    get("[data-steps]").append(node);
    return node;
  });

  function render() {
    const flags = session.state?.flags ?? 0;
    const blocked = !state || writing || session.busy || !!session.restoring || !!(flags & SF.RESTORING);
    const song = !!(flags & SF.SONG);
    controls.forEach((input, i) => {
      input.disabled = blocked || song;
      if (state && document.activeElement !== input) input.value = state.knobs[i];
    });
    selectTrack.disabled = writing;
    roll.disabled = blocked || song;
    kit.disabled = blocked || !!session.state?.playing;
    undo.disabled = blocked || song || !state?.undo;
    undo.textContent = state?.redo ? "Redo" : "Undo";
    for (const node of [protect, clear, free]) node.disabled = blocked;
    steps.forEach((node, i) => {
      node.hidden = i >= (state?.length ?? 0);
      node.disabled = blocked;
      node.setAttribute("aria-pressed", String(!!state?.kept[i]));
    });
    const status = feedback || state?.message || (state?.pending ? "Waiting for the selected playback boundary." :
      song ? "Stop the song to generate a pattern." : state ? "FM-1 connected." : "Reading GRIND controls…");
    if (get("[data-status]").textContent !== status) get("[data-status]").textContent = status;
  }
  async function refresh() {
    if (!active || writing || reading || session.busy || session.restoring) return;
    reading = true;
    const target = track;
    try {
      const next = await session.request(CMD.GRIND_GET, { track: target });
      if (!active || target !== track || writing) return;
      if (state && (next.pending !== state.pending || next.commits !== state.commits ||
          next.message !== state.message || next.undo !== state.undo || next.redo !== state.redo ||
          next.knobs.some((value, i) => value !== state.knobs[i]))) feedback = "";
      state = next;
      if (waiting !== undefined && !next.pending) {
        feedback = next.message || (next.commits !== waiting ? "Pattern applied." : "Pattern change canceled.");
        waiting = undefined;
      }
    } catch (error) {
      if (active) feedback = error.message;
    } finally {
      reading = false;
      if (active) render();
    }
  }

  async function send(action, index = 0, value = 0, message = "") {
    if (!active || writing || !state) return;
    writing = true;
    feedback = "";
    const commits = state.commits;
    render();
    try {
      await session.request(CMD.GRIND_SET, { track, action, index, value });
      state = await session.request(CMD.GRIND_GET, { track });
      if (action === GRIND.KNOB) {
        waiting = state.pending ? commits : undefined;
        feedback = state.message || (state.pending ? "" : state.commits !== commits ? "Pattern applied." : "Pattern change canceled.");
      } else feedback = message;
    } catch (error) {
      feedback = error.message;
    } finally {
      writing = false;
      if (active) render();
    }
  }

  const off = [session.on("state", ({ state: current, prev }) => {
    if ((current.flags ^ (prev?.flags ?? 0)) & SF.SONG) feedback = "";
    render();
  }), session.on("busy", render), session.on("restore", render), session.on("resync", () => refresh())];
  const hide = () => {
    active = false;
    clearInterval(timer);
  };
  render();
  return {
    show() {
      hide();
      active = true;
      refresh();
      timer = setInterval(refresh, 500);
    },
    hide,
    destroy() {
      hide();
      off.forEach((unsubscribe) => unsubscribe());
    },
  };
}

export const tab = { id: "grind", title: "GRIND", cap: CAP.GRIND, mount };
