// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The AudioContext is the simulator's clock: when a phone suspends or interrupts it (backgrounding, a call, a
// route change) the whole device stops, screen included, until it runs again.

// should the page call resume() on a context in this state?
export function needsResume(state) { return state === 'suspended' || state === 'interrupted'; }

// iOS mutes Web Audio with the silent switch on unless the page asks for a playback session (Safari 16.4+);
// -> true when it could ask
export function playbackSession(nav) {
  try {
    if (!nav.audioSession) return false;
    nav.audioSession.type = 'playback';
    return true;
  } catch {
    return false;
  }
}
