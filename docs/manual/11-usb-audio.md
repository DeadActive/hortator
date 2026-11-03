# USB audio
<!--blurb: Record the master, or all eight tracks on their own channels, into a DAW over USB.-->

## Overview

The FM-1 is a USB audio input as well as a MIDI port: a DAW, AUM or Audio MIDI Setup records it over the cable, 16-bit, 44.1 kHz. Hold [[HOME]] for the menu and pick **USB AUDIO**; [[KNOB 1]] on the row steps through the modes and [[OCT+]] steps on and wraps.

| Mode | Channels | Device name |
| --- | --- | --- |
| **2 CH** (default) | the stereo master, exactly as before | Hortator |
| **10 CH MIX** | channels 1-2 the master L R (with every effect); channels 3-10 the tracks T1..T8, one mono channel each | Hortator 8ch |
| **10 CH FX** | channels 1-2 only the send effects (chorus, delay, reverb returns, THROW's share); channels 3-10 the tracks T1..T8 | Hortator 8ch |

Hosts that read channel names show Main L, Main R, T1 .. T8.

## What the track channels carry

T1 .. T8 are the track as the mix gets it: its RESON, FILTER, DIST, SLICER, mute, COMP duck and LEVEL, **before** PAN, the CHOR / DLY / REV sends and the master's effects and limiter. They are at the MASTER level (or full level with USB LEVEL FIXED).

In **10 CH FX**, the FM-1's effect buses are mono, so the L and R of channels 1-2 are equal. T1 .. T8 panned in the DAW plus this pair make the whole mix, each part on its own fader.

## What the channels cannot give back

The **master chain**, which only channels 1-2 in MIX (and 2 CH) carry: PERFORM's master effects (REPEAT, REVERSE, TAPE STOP, FREEZE, OCT, LPF, HPF, CRUSH; its track mutes and THROW are in the channels), METRO, SPEAKER EQ (LOWCUT, BASS+), the DC blocker, the limiter and the soft clip.

COMP's ducking is a gain on each ducked track, so the T channels and their sends are already ducked. The FM-1 pans with full level on both sides at the centre: set the DAW's pan law to 0 dB to match it. Each channel is 16-bit; a single track or the sum may reach full scale where the FM-1's limiter would have held the master down (the test: T1 .. T8 panned plus FX equal the master bus before its chain within 18 LSB).

## Switching

- **MIX <-> FX** changes channels 1-2 at once (a 6 ms cross-fade, no reconnect: the same device).
- **2 CH <-> 10 CH** reaches the computer when the menu closes: the FM-1 leaves USB for about a second (`USB RECONNECT: 10 CH` in the top bar) and comes back as the other device. The sequencer and sound play on. MIDI in and out work again once the computer has found the device (a DAW may list Hortator and Hortator 8ch as two devices: choose the one shown).
- Following **CLK USB**, the clock pauses with the cable (`CLK USB PAUSED`); the steps wait up to 2 s after the reconnect, then stop as when the cable is pulled.
- USB AUDIO is a **device setting**, saved, not part of projects. A **safe start** is always 2 CH, and the installers and the editor find the FM-1 under both names.

## Computers and limits

- **macOS and iPadOS** take 10 channels (class-compliant USB audio 1.0). **Linux** takes them too.
- **Windows may not accept 10 CH** (its USB audio 1.0 driver and the channel layout): use 2 CH there.
- 10 CH uses about 920 bytes of every 1 ms USB frame (2 CH: 184): plug the FM-1 straight into the computer or a hub with little else on it.
- **10 CH needs the standard, non-Bluetooth build.** The Bluetooth version has no USB AUDIO row and is always 2 CH (a 10 CH setting saved by the standard build is not read there, and a settings save there clears it).
