# Web editor
<!--blurb: Edit the FM-1 from your browser over USB: sound, presets, projects, patterns and your own samples.-->

## What it is

The Hortator editor is a web page that talks to the FM-1 over USB MIDI. It shows the FM-1's pages as sliders, loads presets, backs up and restores projects, edits patterns step by step, and loads your own sounds into the user sample slots. It needs a **Hortator FM-1 with firmware 1.0.0 or later** (not the stock firmware, not Felucca, not an older Hortator).

Open it at <https://deadactive.github.io/hortator/webapp/editor/> (the site's "Editor" link). Nothing is installed.

## Open it and connect

1. Connect the FM-1 to the computer with its USB cable. USB only: Bluetooth is not an editor link. The Bluetooth version of Hortator has the editor over USB like the plain one.
2. Use **Chrome, Edge or Opera** (computer or Android). **Firefox** asks for a site permission first. **Safari** has no Web MIDI and cannot run the editor.
3. Click **Connect**. The browser asks for MIDI access **with SysEx**: allow it.
4. The page asks each port named Hortator, Felucca or FM-1 who it is, and connects to the first that answers as a Hortator. This works even when the computer still shows the FM-1 under an old name such as "Felucca" or "FM-1". Nothing is sent before you click Connect, and other MIDI gear receives nothing unless you click Search other MIDI ports.
5. If nothing answered, the page says so and offers **Search other MIDI ports**, which asks every other port the same question. An FM-1 that does not answer (stock firmware, Felucca or a Hortator before 1.0.0) gets a link to the installer.

While connected, the **header bar** stays on top: the FM-1 and its firmware and port, PLAY or STOP, the four project slots A to D (`*` unsaved changes, a dashed box a queued slot, a dim one an empty slot), the variation (`VAR 3`, with an arrow to the queued slot or variation), a status (RESTORING, SONG, or busy), and the eight track buttons 1 to 8. A track button selects that track on the page and on the FM-1; selecting on the FM-1 moves it in the page too.

The tabs are **Sound**, **Presets**, **Projects**, **Patterns** and **Samples**. The tab you used last opens next time. A tab appears only if the FM-1's firmware has what it needs.

### Disconnect, reconnect, one tab

- **Reconnect** appears in the header when the FM-1 stops answering (typically during a firmware update). The editor waits, asks again by itself with growing pauses, and gives up after about two minutes busy ("FM-1 busy: Reconnect when the update is done"). Click Reconnect when the update has finished.
- A firmware update always comes first. While the web installer runs, the editor goes quiet; it connects again afterwards. Other SysEx that a DAW sends does not interrupt the editor.
- Unplugging the FM-1 says "The FM-1 was unplugged." and returns you to the start page.
- **One browser tab at a time** runs the editor. A second tab says "The editor is open in another tab" and offers **Use here**, which disconnects the other tab and takes over.
- Adding `?mock` to the address runs the page against a simulated FM-1 (no hardware needed).

## Sound

The Sound tab shows **the selected track's pages** (SOUND for its model, LAYER, FILTER, FX, INSERT, SRR, RESON, SLICER, LFO 1 and 2, DEST) under "Track", and **the globals** (among them GLOBAL, SYSTEM, DLY, REVERB (ROOM, SPRNG, HALL) and CHORUS) under "Global". Every knob has the FM-1's own text beside it (`-12`, `1/8`, `64 %`) and a slider or a menu.

- It works **both ways**. Turn a knob on the FM-1 and it moves in the page (the value flashes). Move it in the page and the FM-1 plays it at once.
- Choosing a track in the page selects it on the FM-1, and the other way round.
- The heading shows the track, its model, and `(edited)` once a knob has moved since a preset loaded.
- Changing MODEL, the resonator model or the INSERT type re-reads the track, because the knobs underneath change.
- The pages are read from the FM-1, so a knob a newer firmware adds appears by itself.

## Presets

Left: the **151 factory presets** in three banks (ALL, MD, MODERN, CLASSIC), with a search box. Pick one and click **Load onto track N** (or double-click). A factory preset sets the sound only.

Right: the **96 user presets** as a list, each with a number, a name (up to 24 characters), its model, a tag and buttons.

- **+FX / SOUND tag.** A preset tagged +FX also sets the track's FX when loaded (the sends, FILTER, INSERT, SRR and RESON); SOUND loads the sound only. The **FX** button opens the preset's FX values for editing; untick "Loading it sets these FX" to make it sound-only.
- **Save T3** (the button shows the selected track) puts that track's sound **and its FX** in the slot (it asks before replacing a used one). **Load** loads the slot onto the selected track. **x** deletes. The arrows or dragging reorder.
- The list is edited in the page. Nothing reaches the FM-1 until you click **Write to FM-1** (the page counts the slots that differ, marked `*`). **Read FM-1** reads the list again; **Back up** saves it to a `.json` file; **Open file...** loads a file into the list (then Write to FM-1). A file can come from another FM-1.
- The preset **names live in the page and its files**. The FM-1 itself keeps showing the model and number.
- Writing needs the FM-1 **stopped**. A progress bar and **Cancel** show; a cancelled or failed write leaves the FM-1 with its old presets.
- Older files and backups (128 presets, no FX) still open: presets past 96 move into the first empty slots, the way the FM-1 moves its own, and the page says how many did not fit. An FM-1 still on a firmware with 128 presets is read but not written: update it first.

## Projects

Four cards, **A to D**, show what is **saved in the FM-1's flash**: the name, the variations in use, the number of locks, the size, and a status (saved, unsaved changes or empty). The loaded slot says "loaded".

- **Back up** (on a card) saves one project, with its locks and variations, to a `hortator-project-A-<date>.json` file.
- **Restore here...** reads a project file into that slot (it asks first and says what it replaces). A project file can come from another FM-1.
- **Copy to...** copies a saved project to another slot.
- **Back up all** saves everything the FM-1 loads after a power cycle: the 96 user presets (with their FX) and the four projects, in a `hortator-backup-<date>.json` file. **Restore all...** puts it back, replacing the user presets and the projects (it says which and asks first).
- **Read again** reads the cards from the FM-1. **Abort** stops a running restore until it commits.

A backup holds **what is saved**, not what is only in the FM-1's memory. A card marked "unsaved changes" tells you to **SAVE on the FM-1 first** (or use Save on FM-1 in the Patterns tab).

Restores and copies run only while the FM-1 is **stopped**. During a restore the FM-1 shows `RESTORING`; its PLAY, cue keys, LOAD, SAVE, the TOOLS pastes, DUPLICATE and CLEAR, and a held step's knobs wait, and nothing is recorded. A power cut in the middle leaves the old or the new project whole. A file made by a newer editor than the FM-1 understands is refused with `UPDATE THE FM-1 FIRST`, and the FM-1 keeps what it has. Backups made by earlier test builds (before NUDGE) still restore, every step at NUDGE 0.

## Patterns

The Patterns tab edits the sequence of the **loaded slot**.

- **CUE.** The keys A to D and VAR 1 to 8 queue a slot or variation exactly as the FM-1's cue keys do, with the same refusals (`VARIATIONS FULL`, `STOP SONG TO CUE` ...). The playing one is lit, a queued one blinks, `*` is unsaved.
- **SHOW** picks which slot and variation the grid displays (not what plays). Only the loaded slot is editable; another slot is read-only ("cue it to edit"). **Follow the FM-1** puts the grid back on what plays.
- **The grid** has 8 tracks by 64 steps. A cell shows the step: on, accent, a ratchet count, `%` for a PROB below 100 %, `F` / `!` for FILL / !FILL, and marks for NUDGE (left early, right late) and locks. Click a step to turn it on or off; shift-click selects it without changing it. Steps past the track's LEN are dimmed.
- **The step panel** edits the selected step: ON, ACC, SLIDE and TIE (ACID tracks), NOTE (0 to 127, shown as a note name), PROB (0 % to 95 %, 100 %, 1-SHOT, the fractions 1/2 to 8/8, FILL, !FILL), RATCH (1 to 4) and NUDGE (a slider, -50 to +50 hundredths of a step, in the FM-1's own reading).
- **Locks** lists the selected track's locks (knob, step, value; MOTION events read-only). Edit a lock's slider, clear one with x, use **Add a lock...** on the selected step, **Clear step** or **Clear track**. Locks belong to the slot: they apply in every variation of it.
- **Editing a variation that is not playing makes the FM-1 switch to it,** as pressing its key would. While playing, that switch is queued and the edit is made when it arrives.
- **Save on FM-1** is the FM-1's SAVE for the loaded slot (stopped only).
- **Copy variation...** copies a variation into a variation of the **loaded** slot, from the FM-1 (another slot and variation) or from a project or backup file. Tick the box to replace the loaded slot's locks with the source's too. Every step's NUDGE comes along. From a file, MOTION events are not copied (the editor writes locks only). Locks the FM-1 cannot hold are refused before anything changes. To copy into another slot, cue that slot first.

## Samples

The Samples tab loads your own sounds into **USR1, USR2 and USR3**. Each slot holds a **set of up to 16 samples**; the SAMPLE model's and the LAYER's SET knob picks USR1 to USR3, and SLOT picks one of the set's samples in the order the page lists them (`2/5`).

Each slot's card shows the set's name, how full it is, the room left, and its samples (number, name, length, size) with the buttons **Play** (as the FM-1 plays it), **Edit** (open it again to trim, normalize or resample), the arrows (move), **Name** and **Del**.

### Load a sound

1. **Load a sound file...** and choose a WAV (or an AIFF your browser can open). It is mixed to mono.
2. Drag **Start** and **End** to trim it on the waveform.
3. Choose **Set** (USR1 to USR3) and **Into**: add as the set's next sample, put it **in place of** one, or **replace the whole set**. Give it a **Name** (8 characters).
4. Tick **Normalize** if you like, and choose the **Rate**: the file's own up to 44.1 kHz, or a lower one (32, 22.05, 16 or 11.025 kHz) when the sound is too long for the room. The page shows the length you get.
5. **Preview** plays exactly what the FM-1 will play (the page encodes it as the FM-1 stores it). Click **Add**.

### Chop

**Chop...** cuts a long sound (a drum loop, a phrase) into samples:

- **Auto** finds the hits; a **Sensitivity** slider decides how many.
- **Beats** cuts every 1/4, 1/8 or 1/16 at a **Tempo** you type (30 to 300) or **Detect**; **÷2** and **×2** fix a half or double guess.
- **Equal** makes as many slices as you ask (2 to 64).

The cuts show on the waveform: click to add one, drag to move it, double-click to remove it; click a slice's number to hear it. The slices that fit go in as samples named after the file (`LOOP 1`, `LOOP 2` ...); the page says how many did not fit (no room, or the set's 16 samples).

### Writing, sets and limits

- Adding, replacing and chopping write at once. Moving, renaming or deleting samples waits for **Write USR1** (the card says it has changes; **Undo changes** reads the set back; **Save as file** keeps it). Every write sends **the whole set** with a progress bar and **Cancel**; the FM-1 shows `UPLOAD USR1` and must be **stopped**.
- Until an upload is complete the slot reads as empty, so a cancelled upload, a pulled cable or a power cut leaves the slot empty, never half a sample. The page keeps the set so you can write it again or save it as a file.
- **Back up** saves a slot to a `.fsmp` file; **Restore...** puts one into any slot. **Rename set...** names the set (8 characters, the name the FM-1 shows); **Clear** empties it.
- A sample never spills past its slot: the projects' variations and lock banks next to USR2 and USR3 stay safe, and a firmware update leaves the samples alone.
- Room: USR1 holds 80 KiB, USR2 and USR3 48 KiB (seconds per rate are in Sounds, Samples). A set of two or more samples uses a little room per sample for its names and start table.
- A set made elsewhere (for example a multi-zone set from upstream Felucca) is listed by its samples; changing it lays it out again as plain samples, after asking. Sets and single samples from before 1.0.0 load and play as they did.
- START, END and LOOP work within whichever sample SLOT plays, over that sample's own length.

## Limits and messages

- **Stopped only:** user-preset writes, project restores and copies, and sample writes need the FM-1 stopped. Otherwise the FM-1 answers "Stop the FM-1 (PLAY), then try again. Nothing was written."
- **Clock out:** while the FM-1 plays with SYNC (MIDI clock out) on, a big transfer could make the clock jitter. The page asks "Clock out may jitter during the transfer - stop first?" with **Stop**, **Continue** and **Cancel**. Stop asks you to press STOP on the FM-1 and starts the transfer when it stops.
- **Another restore running:** "Writing <what> to the FM-1 (<owner> tab)": wait for it.
- **FM-1 busy (update?):** the FM-1 answered nothing (an update, or the cable). The editor waits and asks again by itself; after about two minutes it stops, and you click **Reconnect**.
- **No flash this boot** (`NO FLASH`): power-cycle the FM-1.
- **Restores and writes are all-or-nothing:** a cancel, an error or a power cut keeps the old data.

## Troubleshooting the editor

| Symptom | Fix |
| --- | --- |
| "This browser has no Web MIDI" | Use Chrome, Edge or Opera (desktop or Android) or Firefox. Safari cannot. |
| "MIDI access was refused" | Allow MIDI **with SysEx** for the page in the browser's site settings, then Connect again. |
| "No Hortator FM-1 found" | Plug in the FM-1 by USB (Bluetooth is not an editor link), close other programs that hold its MIDI port, Connect again. If it shows under another name, click **Search other MIDI ports**. |
| "This FM-1 does not answer the editor" | It runs the stock firmware, Felucca or a Hortator before 1.0.0, or it is updating. Install 1.0.0 or later from the installer link. |
| "The FM-1 is in update mode" | Finish the update in the installer, then connect again. |
| "...runs a newer Hortator" | Use the editor from that firmware's release. |
| "The editor is open in another tab" | Use it there, or click **Use here**. |
| A tab is missing | The firmware lacks that feature; update the FM-1. |
| A write is refused | Stop the FM-1 first; read the message under the buttons. |
