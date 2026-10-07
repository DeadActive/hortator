# Recovering an FM-1 from a Mac (boot mode, no extra hardware)

Use this when the web installer and M-VAVE's updater no longer reach the FM-1. That happens when the firmware on
it starts but its update entry does not answer. The guide puts M-VAVE's stock firmware back through the chip's
built-in boot mode, from a Mac, with the usual USB cable. Then install Hortator again from the web installer.

How it works: the FM-1's chip (JieLi WL82) has a USB boot mode in its mask ROM, which no firmware can erase.
`tools/rescue/fm1_uboot.py` talks to that boot mode. It loads a small flash helper into RAM, backs up the
whole flash, and rewrites only the firmware area (0x4000 to 0x93000). Each 4 KiB sector is erased, written,
read back and compared. The boot area, the chip key and the data area (settings, samples) are never written.

Recorded on 2026-10-08: an FM-1 whose firmware had a broken update entry was backed up and restored to stock
V15 this way, from an Apple-silicon Mac. That run used the session scripts this tool was made from: the same
commands, the same order, the same checks. The tool itself is tested against a simulated FM-1
(`tests/fm1_uboot_test.py`).

**Use at your own risk.** Read each step before you run it, and stop at the first step that does not go as
written.

## What you need

- A Mac with [Homebrew](https://brew.sh), Python 3 and a terminal.
- `libusb` and `pyusb`:

  ```
  brew install libusb
  python3 -m venv ~/fm1-rescue
  ~/fm1-rescue/bin/pip install pyusb
  ```

- The flash helper `wl82loader.bin` (24064 bytes) from
  [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool). It is not part of this repository:

  ```
  git clone https://github.com/kagaimiq/jl-uboot-tool ~/jl-uboot-tool
  ls -l ~/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin
  ```

- M-VAVE's stock firmware file `FM-1.fwsc` (V15) from <https://www.m-vave.com/download>. Check it:

  ```
  shasum -a 256 ~/Downloads/FM-1.fwsc
  ```

  The sum must begin with `db1642b2`. The tool refuses any other package.
- This repository (`git clone https://github.com/DeadActive/hortator`), or just its `tools/rescue/fm1_uboot.py`.
- A charged FM-1 and a USB data cable that has worked with it before.

In the commands below, `RESCUE` is short for:

```
RESCUE="sudo $HOME/fm1-rescue/bin/python3 tools/rescue/fm1_uboot.py"
LOADER=$HOME/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin
```

Run them from the repository folder. `sudo` is needed because macOS's own USB storage driver holds the
boot-mode device, and only an administrator can take it over. The tool reads and writes the FM-1 only. It
changes nothing on the Mac except the backup file.

## 1. Put the FM-1 in boot mode

Use one of these:

- **Hortator or Felucca is running.** Hold **OCT−** and **OCT+** together. A countdown appears after 2 s, and
  letting go cancels it. At 5 s the screen shows `UBOOT` and the FM-1 enters boot mode.
- **From the computer.** Send the SysEx message `F0 22 24 35 7D F7` to the FM-1's MIDI port. It works the same
  way on stock V15 and on Felucca-based firmware.
- **The firmware crashes right after start.** Two crashes within 30 s of starting enter boot mode by
  themselves (Felucca-based firmware).

The screen goes black or keeps the last picture, and the FM-1 stays in boot mode until it is switched off or
the tool finishes. Check that the Mac sees it:

```
system_profiler SPUSBDataType | grep -A3 WL80UBOOT
```

You should see `WL80UBOOT1.00` (vendor ID `0x4c4a`, product ID `0x8057`). Close other apps that use the FM-1
over MIDI, and the installer pages.

If the FM-1 shows nothing and the Mac sees neither the FM-1 nor `WL80UBOOT`, this guide cannot help: use
[FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter) (a Seeed XIAO RP2040 wired to the FM-1's
USB lines). It can force boot mode with no working firmware at all.

## 2. Back up the whole flash (reads only)

```
$RESCUE backup --loader "$LOADER" ~/fm1-backup.bin
```

Expected output:

```
chip key 0x980F, flash type 3 id 0x856014
read 1: 2.3 s, sha256 …
read 2: 2.3 s, sha256 …   (the same sum as read 1)
saved /Users/you/fm1-backup.bin; nothing was written to the FM-1
```

- `not an FM-1 as expected`: stop here. Nothing was read or written.
- `the two reads differ`: run it again, with another cable if it repeats.
- `cannot take the device from macOS`: you left out `sudo`.

When the tool ends, the FM-1 restarts and boots its firmware as before. Keep `fm1-backup.bin` somewhere safe:
it is this FM-1's flash, including its own data area.

## 3. Check the restore (writes nothing)

Put the FM-1 in boot mode again (step 1), then:

```
$RESCUE restore --loader "$LOADER" ~/Downloads/FM-1.fwsc ~/fm1-backup.bin
```

Expected output: the chip line again, `143 of 143 app sectors differ from stock V15` (fewer if part of it is
already stock), and `DRY RUN: nothing written (add --write)`.

The check refuses in these cases:

- **The package is not stock V15** (`is not M-VAVE's stock V15 FM-1.fwsc`): get the file from M-VAVE again.
- **The backup does not match this FM-1** (`head or data differ from the backup`): the backup is of another
  FM-1, or this one changed since. Make a new backup (step 2).

## 4. Restore the stock firmware (writes)

Put the FM-1 in boot mode again (step 1), then:

```
$RESCUE restore --loader "$LOADER" ~/Downloads/FM-1.fwsc ~/fm1-backup.bin --write
```

Do not unplug the cable or switch the FM-1 off until it prints `done`, about 15 seconds. The recorded run
wrote 143 sectors in 7.9 s, with a full read before and after. You will see progress every 16 sectors, then:

```
done: stock V15 app written and verified. Unplug USB or power-cycle the FM-1.
```

- `verify failed, again`: one retry, nothing to do.
- `failed 3 times: STOP` or `FINAL CHECK FAILED`: leave the FM-1 switched on and in boot mode, and run the
  same command again. If it stopped, put it in boot mode again first. It writes only the sectors that are
  still not stock, so running it again is safe. The same goes for a broken-off run, for example a pulled
  cable.

## 5. Start the FM-1 and reinstall Hortator

1. Unplug USB or switch the FM-1 off and on. It starts the stock M-VAVE firmware.
2. Install Hortator from the web installer as usual: <https://deadactive.github.io/hortator/>.

The data area above `0x93000` (settings, presets, user samples) was not touched: it is as it was in your
backup.

## How it works

- **Boot mode.** The chip's boot mode is USB mass storage (`WL80UBOOT1.00`), driven with vendor SCSI
  commands. The tool loads jl-uboot-tool's flash helper into RAM at `0x1C02000` and starts it. The helper
  reports the chip key (`0x980F` on the FM-1) and the flash chip (`0x856014`, 1 MiB). It then reads 512-byte
  blocks, erases 4 KiB sectors, and writes 512-byte blocks.
- **The stock image.** M-VAVE's `FM-1.fwsc` (V15) holds the raw flash image from address 0 to `0x93000`, at
  offset `0x414` in the file. Its first 16 KiB (the boot area) equal the FM-1's own, which the tool checks
  before using the image.
- **Credits.** The protocol details come from [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter)
  (`docs/PROTOCOL.md`). The cipher and the command framing are ported from
  [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (MIT, Andrey Grigoryev).
