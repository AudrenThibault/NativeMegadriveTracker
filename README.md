# GeneTrackerMD

A music tracker that runs **on the Sega Mega Drive** — not a desktop editor
that exports to the console, but the tracker itself, gamepad in hand, on the
machine.

Ten voices: six FM channels from the YM2612, three tone channels and one noise
channel from the SN76489, plus a PCM voice that replaces the sixth FM channel
when you give it a sample.

The workflow is LSDJ's — song, chains, phrases, tables — because that is the
one your fingers already know.

## What it does

- **Sequences** all ten voices, through the song → chain → phrase chain.
- **Edits FM instruments**: four operators, their eleven parameters, algorithm,
  feedback, LFO.
- **Plays PCM samples of any length.** The Z80 reads them straight from the
  cartridge and walks the bank window on its own — a one-second sample plays
  without a hiccup.
- **PSG macros** — volume, arpeggio, noise mode — and a three-point envelope.
- **Saves** into the cartridge's own memory: sixteen slots, kept when the
  console is switched off (FRAM, no battery).
- **Exchanges songs** with its Nintendo DS sibling, samples included.

## Controls

Three buttons, and **A plays the role of LSDJ's SELECT**: it is the modifier
that moves you between screens.

### Moving around

| Input | What it does |
|---|---|
| D-pad | move the cursor |
| **A + right** | go one screen down: SONG → CHAIN → PHRASE → INSTRUMENT → TABLE |
| **A + left** | go back up |
| **A + up** | open the PROJECT screen |
| **A + down** | leave it |
| **B + up / down** | jump sixteen rows, in SONG |
| START | start and stop playback, from any screen |

### Editing

| Input | What it does |
|---|---|
| **C** | put a value in an empty cell |
| **C + right / left** | step the value up, down |
| **C + up / down** | step it in large increments |
| **B + C** | clear the cell (either order) |

### Copy, paste, clone

| Input | What it does |
|---|---|
| **A + B** | arm a selection — the D-pad then extends it, the cursor stays on the anchor |
| **B** | copy the selection |
| **A + C** | paste. In SONG the paste **inserts**: everything below moves down |
| **A + B then C** | deep clone — the chain and its phrases are duplicated into fresh slots |

### Muting a voice

| Input | What it does |
|---|---|
| **hold B, then A** | mute the voice under the cursor, in SONG |
| **B** | unmute it |

Order matters: A then B arms a selection, B then A mutes. A muted voice keeps
advancing through the song without sounding, so it comes back in place, with
no drift. Its name is shown inverted in the header.

### Saving

`PROJECT → LOAD/SAVE SONG`. You always arrive on **LOAD**; one press right
reaches **SAVE**, a second **ERASE** — which asks for confirmation. In SAVE
the cursor lands on the row of the song you are working on, with its name
already filled in: `C`, `C`, `C` writes a newer version.

In the name window, the D-pad walks the letter grid, `C` types, `<` deletes,
`OK` confirms, `B` cancels.

## Building

One prerequisite:

```sh
brew install m68k-elf-gcc
```

Then, to build a ROM carrying a song:

```sh
python3 outils/bibliotheque.py verser morceaux/TUTU.MDM
```

That rebuilds the sample bank, compiles the ROM, and drops it on the
EverDrive's SD card if it is mounted. `./build.sh` on its own compiles without
changing the embedded song — useful to check that it still builds, not to test
a song.

For the **bare ROM**, the one that gets published — the tracker alone, with no
song and no samples:

```sh
python3 outils/bibliotheque.py vierge
```

It opens on an empty project and saves into the cartridge's sixteen slots like
any other build: saves live in FRAM, not in the ROM.

The built ROM lands in `release/`. That directory is not tracked — the binary
belongs on the Releases page, not in the repository.

## The ROM carries its own map

A GeneTrackerMD ROM holds a descriptor marked `GENETRK-PLAN01`: it says where
the songs and the sample bank live, and how much room is reserved for them.
That is what lets another tool — the DS tracker, for instance — write into the
image **without recompiling it**.

## Hardware

Developed and verified on a **PAL Mega Drive** with an **EverDrive MD V3**.
Debugging leans on a test bench that runs the ROM inside a headless emulator
core and captures both sound and picture, and on a log the tracker writes into
its own save memory — that is how the defects were found, rather than guessed.

## Licence

GNU General Public License version 3 — see [LICENSE](LICENSE).

Copyright (C) 2026 Audren Thibault

This program comes with **absolutely no warranty**. You are free to
redistribute and modify it under the terms of the GPL v3.

GeneTrackerMD belongs to a family: **GeneTracker** on iPad, **GeneTrackerMD**
here, **GeneTrackerDS** on the Nintendo DS. The three are independent projects
— code is copied between them, never referenced — and **the licence of this
repository covers the Mega Drive version only**.

### Additional term (GPL v3, section 7)

Section 7(b) of the licence allows an author to require that attribution be
preserved. This project uses it, and it is the only condition added:

> **You must keep, in the source code and in the legal notices the program
> displays (the ABOUT page), the author credit "Audren Thibault" and the
> address of the original repository
> `https://github.com/AudrenThibault/NativeMegadriveTracker`.**

In other words: do what you like with it, modify it, redistribute it, even
sell it — but **the name and the link stay**, in the files as on screen.
