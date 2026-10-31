// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The firmware's editor frames (tests/editor_test.c FILE: every reply and push it sent, hex, a line each) read with the
// page's codec (web/editor/proto.mjs) and written again: each must decode and re-encode byte for byte, so the
// firmware's field layout is the protocol's (web/EDITOR_PROTOCOL.md). Also INFO's numbers = device_data.json's.
//   node tests/editor_wire.mjs build/host/editor_frames.txt
import { readFileSync } from "node:fs";
import { CMD, decodeFrame, encodePush, encodeReply, isEditorFrame } from "../web/editor/proto.mjs";

const data = JSON.parse(readFileSync(new URL("../web/editor/device_data.json", import.meta.url), "utf8"));
const lines = readFileSync(process.argv[2], "utf8").split("\n").filter(Boolean)
  .filter((h) => isEditorFrame(Uint8Array.from(h.match(/../g).map((x) => parseInt(x, 16)))));   // (not the test's ota frame)
const seen = new Set();
let bad = 0, info = null;
for (const hex of lines) {
  const b = Uint8Array.from(hex.match(/../g).map((x) => parseInt(x, 16)));
  let again;
  try {
    const d = decodeFrame(b);
    again = d.push ? encodePush(d.cmd, d) : encodeReply(d.cmd, d.seq, d.rc, d.rc ? { detail: d.detail, msg: d.msg } : d);
    seen.add(`${d.push ? "push" : "reply"} ${d.cmd}${d.rc ? " rc" : ""}`);
    if (!d.push && !d.rc && d.cmd === CMD.INFO) info = d;
  } catch (e) {
    console.log(`FAIL  ${hex}: ${e.message}`);
    bad++;
    continue;
  }
  if (again.length !== b.length || again.some((x, i) => x !== b[i])) {
    console.log(`FAIL  ${hex}: re-encoded differently`);
    bad++;
  }
}
const c = data.consts;
if (!info || info.pcount !== c.P_COUNT || info.gcount !== c.G_COUNT || info.nmodels !== c.NMODELS ||
    info.nfact !== c.PRE_NFACT || info.nuser !== c.PRE_NUSER || info.ntrk !== c.NTRK || info.nstep !== c.NSTEP ||
    info.nbanks !== data.banks.length || info.nvar !== c.VAR_N) {
  console.log("FAIL  INFO's numbers differ from web/editor/device_data.json");
  bad++;
}
console.log(`${bad ? "FAIL" : "ok  "}  ${lines.length} firmware frames (${seen.size} kinds) decoded by proto.mjs and ` +
  "re-encoded byte for byte; INFO = device_data.json");
process.exit(bad ? 1 : 0);
