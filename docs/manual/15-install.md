# Install, update and recovery
<!--blurb: Installing Hortator, the two versions, safe start, going back to stock, and Mac recovery.-->

## Install

1. Open the [web installer](https://deadactive.github.io/hortator/webapp/installer/) in **Chrome or Edge** on a computer.
2. Connect the FM-1 straight to the computer with a USB **data** cable. No probe or Transporter is needed.
3. Press **Install** and keep the cable in until it says *Done*. The FM-1 restarts with Hortator.

Before installing: keep M-VAVE's official updater and stock firmware at hand, note the current firmware version, charge the battery and use a known-good cable. After power-on, let the FM-1 run 30 s: a restart inside the first 30 s counts as a failed start, and two in a row enter the built-in bootloader.

### Two versions

| Version | File | Notes |
| --- | --- | --- |
| **Plain** (default) | `hortator-X.Y.Z.fwsc` | everything except Bluetooth; never enables the radio |
| **Bluetooth** (experimental) | `hortator-X.Y.Z-bluetooth.fwsc` | adds Bluetooth MIDI, FM-Link, SMC-Mixer; no USB AUDIO 10 CH |

The installer offers both with the plain one selected. Releases are on [GitHub](https://github.com/DeadActive/hortator/releases) with `LICENSE`, `LICENSING.md` and `LICENSES.zip`; the changelog says what changed in each.

### Installing from a terminal

`tools/fm1_install.py` installs a package without a browser. It needs `mido` and `python-rtmidi` (use a virtual environment).

```
fm1_install.py PACKAGE.fwsc [--port NAME] [--yes] [--force]
fm1_install.py --info [--port NAME]
```

- `--info` only reads the FM-1's identity.
- It finishes an interrupted update if the FM-1 is still in update mode.
- It refuses a package without the Felucca loader marker unless `--force`.
- Installing M-VAVE's `FM-1.fwsc` (V15) goes back to stock.
- Exit codes: 0 done, 1 cancelled or error, 2 bad arguments or package, 3 FM-1 not found, 4 connection lost, 5 timeout, 6 wrong model or identity after install.

### Names on USB

The running firmware appears as **Hortator** (earlier versions: Felucca), as **Hortator 8ch** in 10 CH USB audio, and as **Felucca Update** while an update is being written. The installers and tools find all of them, so updating from any earlier build works.

### Privacy

The site counts visits and finished installs with GoatCounter (no cookies, no personal data; one anonymous "install" event after the installer says Done). Nothing is counted on a local copy, and `fm1_install.py` sends nothing.

## Updating

Run the installer again with the newer version; it uses the same update path as any earlier build. Projects saved by a newer version may not load in an older one (see *Version history* for each version's compatibility notes).

## Safe start

Power off, **hold SEQ**, power on and keep holding until the screen shows `SAFE START` (`NO AUDIO - USB UPDATE READY`). SEQ is read once, right after the start screen appears. No drum code runs, but USB answers, so you can update or go back to stock. A safe start loads no project, is always USB 2 CH, keeps every LED dark and skips Bluetooth autostart. Power off to leave.

Use it after a `FELUCCA CRASH` screen, repeated restarts or a blank screen.

## Back to the official firmware

Use M-VAVE's updater **M-UPGRADE** from <https://www.m-vave.com/download> (Downloads, PC Software, M-UPGRADE) with the official firmware (PC Firmware, FM-1 V15), or the installer's *Return to official V15*.

## Recovery from a Mac

If the FM-1 stops answering both the installer and M-UPGRADE, a Mac can put the official firmware back through the chip's boot mode with no extra hardware: see `docs/RECOVERY.md` in the repository. The summary:

1. Put the FM-1 in boot mode: hold [[OCT-]] and [[OCT+]] together; a countdown starts after 2 s and the screen shows `UBOOT` at 5 s. (Alternatives: SysEx `F0 22 24 35 7D F7`, or two crashes within 30 s of start.)
2. Run `tools/rescue/fm1_uboot.py` with `sudo`. It needs `libusb`, `pyusb`, the flash helper `wl82loader.bin` from jl-uboot-tool and M-VAVE's stock `FM-1.fwsc` V15 (its SHA-256 must begin `db1642b2`; the tool refuses any other).
3. The tool backs up the whole flash, does a dry-run restore, then a restore with `--write`. It writes only 0x4000 to 0x93000 and never touches the boot area, the chip key or the data area (settings, presets, samples).
4. Install Hortator again from the web installer.

If the FM-1 reaches neither normal mode nor `WL80UBOOT`, only the FM-1 Transporter (an RP2040 board) can recover it.

## Building from source

- The firmware and its package: `BUILDING.md` (`./build.sh`; `DRUM_PACKAGE=1 ./build.sh` for an installable package and the site).
- The web simulator and landing page: `tools/build_sim.sh`. This manual: `tools/build_manual.py`.
- Tests: `tests/run_tests.sh`, `tests/run_drum_tests.sh`. Versions and publishing: `docs/VERSIONING.md`, `tools/publish.sh`.
- Versions are MAJOR.MINOR.PATCH from `VERSION.txt`. MINOR means a new feature or compatible project format; MAJOR means saved data no longer loads. MENU, ABOUT shows `DRUM-<version>`.

## Licence and credits

Code: GPL-3.0-only. Assets and third-party material: `LICENSING.md`. Hortator is by DEADACTIVE, built on Felucca by Leo Kuroshita (Hügelton Instruments). Credits: Grids, Streams and Plaits by Emilie Gillet (Mutable Instruments), DaisySP by Electrosmith, Terminus font by Dimitar Toshkov Zhekov, samples from Versilian Studios (VSCO-2 CE and VCSL, CC0) and the Hügelton Sample Pack, the JieLi AC79 SDK format, jl-uboot-tool by Andrey Grigoryev, and research from Joe Landers' Machinedrum emulator (measurements only). MENU, ABOUT scrolls the full credits. M-VAVE and FM-1 are trademarks of their owners; Hortator is independent firmware, not affiliated with or supported by them. Machinedrum is a trademark of Elektron; Hortator is not affiliated with Elektron.
