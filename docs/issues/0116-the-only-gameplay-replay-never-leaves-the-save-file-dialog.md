---
id: 116
title: The recorded gameplay replay never leaves the save-file dialog, so every looks-right verdict describes a menu
status: resolved
symptom: replays/gameplay/artisans-arrival.pad is still on the "YOU WILL NOT BE ABLE TO SAVE YOUR PROGRESS" warning at its last presented fence, while tools/drive.py reaches Artisans on the same build
state_items: S019, S020
tags: replay, input, gameplay, fps60, widescreen, verification
created: 2026-09-19
updated: 2026-10-01
---

## What happens

`replays/gameplay/artisans-arrival.pad` holds 6,781 recorded frames. Driven through the product at
6,900 fields it produces 3,818 presentation fences and never reaches Artisans. Frames captured at
the three windows the dump can reach all show the memory-card flow:

| fence | what is on screen |
|---|---|
| 1100 | "THE MEMORY CARD IN SLOT 1 DOES NOT HAVE A SAVE FILE / CREATE SAVE FILE NOW?" |
| 3100 | "WARNING! YOU WILL NOT BE ABLE TO SAVE YOUR PROGRESS UNLESS YOU CREATE A SAVE FILE NOW" |
| 3668 | the same warning, unchanged. This is the last frame the replay presents. |

The product is not at fault, and this is not issue 0114. `tools/drive.py gameplay` reaches
`GS_Playing` on the same binary in the same session, at game frame 6360-6380 across two runs, and
the frames it captures are Artisans with terrain, waterfalls and a running Spyro. The difference is
the input mechanism: drive.py OBSERVES guest state and issues pad edges from what it sees, while a
`.pad` file replays recorded edges at recorded frame numbers.

## Why it matters

`external/psxport/tools/port/looks_right.py` takes `--replay`, and this is the only gameplay pad in
the repository. Every verdict produced through it — including the fps60 and widescreen PASSes, and
the frame-time budget in `docs/project-state.md` — therefore describes a dialog box in front of a
mostly static 3D backdrop. Those runs are real and their numbers are real, but they are not
gameplay, and the budget in particular is an under-estimate for that reason.

The fps60 measurement that IS gameplay was taken through drive.py instead; see S020.

## What was not yet known, and the answer

**ANSWERED 2026-10-01, and it is neither of the two candidates as they were framed. The pad stream
did not desynchronise, and the dialog's input handling did not regress: the recording and the card
disagree about WHICH MENU ROUTE to take.** Measured, on the same binary:

| card the replay runs against | what the old raw pad does |
|---|---|
| holds this game's save | reaches `GS_Playing` at game frame ~6340, i.e. plays |
| blank | answers "CREATE SAVE FILE NOW?", sits on the warning, never commits a slot |

Both columns are the SAME 6,781 recorded frames, and they differ only in the card. So the recording
was captured on the save-holding route: its presses are that route's presses, and on a blank card the
guest takes a different route through the front end, so the presses no longer match anything. Nothing
was desynchronised and nothing regressed - the file described a route and said nothing about which
one, and the file's NAME (`artisans-arrival`) claimed the one thing it could not deliver on a card
that had never been written.

The replacement recording is captured ON the blank card (SHA-256 `77d33c6b...30cb`, the canonical
formatted-blank image), so its 28 button-pressing frames include the ones that answer the create-save
prompt, and the card identity in the file is what makes that pairing checkable instead of
accidental. Replayed against that same blank card it reaches Artisans; against the save-holding card
it is now REFUSED by name.

This is the same class of defect as the lineage null in `docs/findings/lineage-metric.md`: a real
measurement of the wrong subject. Here the wrong subject is the CARD, and it was invisible because a
`.pad` file carried no field for it.

## What was done

`looks_right.py` now takes `--route`, a command template carrying `{shot}`, `{settings}` and `{log}`,
and runs it in place of launching the binary with a pad (psxport 48717689 onward). The route owns
when the game is worth looking at and captures one shot itself, so the tool no longer picks a frame
number in advance; it drops the frame cap and the fixed shot frame for a routed run, because both
would cut the drive short or fire wherever it had got to. Nothing about Spyro's menus moved into the
framework: what the route knows stays in `tools/drive.py`, and the template is the whole interface.

The verdicts are now taken as:

```sh
uv run --frozen python external/psxport/tools/port/looks_right.py \
  --binary build/bin/spyro_port --repository . \
  --route 'uv run --frozen python tools/drive.py gameplay --hold left --hold-frames 180 \
           --shot {shot} --settings {settings} --log {log}'
```

## The fix: a phase-keyed, card-identified format (2026-10-01)

Two defects, one file. A recording is only replayable if it says *what it was recorded against* and
*where in the flow each press belongs*, and the old raw format said neither.

**1. CARD IDENTITY, because that is the measured cause above.** Every recording now carries the
SHA-256 of the card image it was made against, and replay REFUSES a different card by name rather
than pressing a menu route it was never recorded on. The refusal names the card it wanted and the
card it got.

**2. PHASE KEYING, for the timing half that was always suspected.** Each recorded pad frame is now
stored as an offset from the entry of the INPUT PHASE it was recorded in, where the phase is an
opaque `uint64_t` the title owns. The framework compares keys and records when they change; it never
learns what a phase means. Spyro 1's key packs `g_Gamestate` with `g_LevelId`
(`titles/spyro1/core/spyro1_input_phase.*`, both words already named in
`game/core/guest_globals.h`). A boot or load that takes a different number of frames now moves the
phase boundary instead of every press inside it.

The old raw format is refused BY NAME, with no silent fallback, because a silent fallback is how a
file called `artisans-arrival.pad` ends up describing a dialog. `tools/psx_pad.py migrate` performs
the conversion deliberately, and it writes ONE UNKEYED ABSOLUTE SEGMENT - that is the honest
description of what a raw file contains, and replay then runs it with the pre-phase semantics rather
than pretending to be keyed.

### The recorded pad, as it now stands

`replays/gameplay/artisans-arrival.pad` is a v1 (`PSXPADPH`) file: 6 segments, 6,502 frames, 28
frames actually holding a button, recorded against the BLANK card SHA-256
`77d33c6be1b8862c8b55d6159ba9a6aed172778a64b6ae9aa705f0638c0330cb` (the canonical formatted-empty
image; the seed is `scratch/saves/pad_seed.mcr`). Its phases, in order, with the frames each one
covers:

| phase | what it is | frames |
|---|---|---|
| `0x000000` | boot prefix (gamestate 0, no level) | 436 |
| `0x0d0000` | title screen, no level yet | 1,351 |
| `0x0d000a` | title screen with Artisans selected | 863 |
| `0x0e000a` | level load (gamestate 14) | 2,942 |
| `0x0d000a` | back to the title, save committed | 770 |
| `0x00000a` | **Artisans, playing** | 140 |

The last row is the claim the file's name makes, and it is the only row that is gameplay.

### Measured legs

**Positive.** The keyed pad replayed with NO input driver at all - nothing taps anything, so every
press in Artisans came out of the file:

    uv run --frozen python tools/pad_replay.py \
        --pad replays/gameplay/artisans-arrival.pad \
        --card-seed scratch/saves/pad_seed.mcr \
        --settings tools/fps60_control_settings.ini --frames 6600

Run on the port built from this branch against the pinned framework, the guest enters
`gs=0/level=0xa` at frame 6361 and the log ends `[padphase] replay COMPLETE: all 6 segment(s)
consumed, 6502 of 6502 recorded frame(s) delivered`; the captured frame is 512x240 at 93.3% non-black
and shows Spyro standing in Artisans with terrain, bridge and towers.

**Negative control, and it did not fail - reported as measured.** The same 6,502 masks, flattened by
a scratch-only script into one unkeyed absolute segment through the production `migrate` path (the
production tool correctly refuses to flatten a keyed file implicitly), replayed under the SAME
settings file: the guest also enters `gs=0/level=0xa` at frame 6361, and
`scratch/padphase/leg4_absolute.ppm` is 93.3% non-black with the same picture. So
`fps60_control_settings.ini` is a genuinely different product configuration and it is NOT a timing
perturbation for this title: the phase boundary sits at frame 435 under it and under
`shipping_settings.ini` alike, and the older scratch traces show 435 across audio, pacing and
rendering variations too. `PSXPORT_RENDER_PATH=gte` was tried as a harsher perturbation and aborts
on Spyro before gameplay, so it cannot serve either.

**What would have been needed and was not found: a setting that moves Spyro's boot/load pad-frame
boundary.** No tracked product setting does. The discriminator therefore cannot be made to
desynchronise the absolute control, and this issue does not claim one. What the positive and negative
legs together DO establish is the smaller claim that is now true and was not before: the keyed file
reaches Artisans from a card-identified route, and the file says which card that is. The card
refusal itself is covered by the framework's negative tests rather than by a game run.

### Re-verified on the rebased tree, and one operational consequence of the card identity

Both legs above were re-run after the rebase onto `origin/main` (psxport `5b66eb7f`, spyro
`72ef433`), against the binary the gate built, with the probe corrected so that "arrived" means
`gamestate 0` AND a non-zero level rather than the first frame that merely reads `gs=0` (which is
also the boot prefix, and which is why an earlier probe reported arrival at frame 0):

| leg | result |
|---|---|
| keyed, no input driver | `ARRIVED IN A LEVEL (gs=0, level!=0) at frame 6361`, `[padphase] replay COMPLETE: all 6 segment(s) consumed, 6502 of 6502 recorded frame(s) delivered`, 512x240 at 93.3% non-black showing Spyro in Artisans |
| absolute (unkeyed) control, same settings | `ARRIVED IN A LEVEL at frame 6361`, `all 1 segment(s) consumed, 6502 of 6502`, same 93.3% |

**The card identity is exact, and `tools/pad_replay.py` owns the leg.** A pad leg is the one run
whose start state is fixed by something outside itself - the recording's card - so its card is
`scratch/saves/pad_replay.mcr`, restored from the seed image `scratch/saves/pad_seed.mcr` before the
run. It is never `scratch/saves/card.mcr`: that is the shared card, and a run that writes it changes
the next run's input (docs/issues/0169). The digest in the file is the card at RECORDING time and the
guest writes a save during the run, so the copies taken after a run no longer hash to it and are
refused by name - the intended behaviour, because the route the file describes belongs to the card it
was recorded against.

`drive.environment(card=)` is deliberately NOT used for a pad leg, and the reason is in the tool's
docstring: `card=` runs `blank_card_environment`, which DELETES the card so the product formats a
fresh one. For a title route that is exactly right, because the requirement is "must repeat". For a
pad leg the requirement is "must start from the recorded state", and deleting the image would
guarantee a refusal. The isolation is identical - a private image, never the shared one - and only
the start state differs. Spyro 2/3's title routes keep the blank-card path unchanged and were
measured on it after this change: 3340 and 4890 fields, the numbers docs/issues/0169 recorded.

### Test counts at the time of writing

| gate | result |
|---|---|
| `psxport` `ctest --test-dir build` | **199/199** - the 198 upstream tests plus `test_pad_phase_replay` |
| `spyro` `uv run --frozen python tools/verify.py --jobs 6` | **130/131**, the one red being `spyro_psxport_pin_live` refusing a DIRTY framework dev clone. That is the pin guard working: this run deliberately built against the uncommitted framework worktree rather than the recorded pin `41373bc0` |
| `tools/drive.py gameplay` | reaches `GS_Playing` at frame 6360, exit 0 - ON A CARD HOLDING THIS GAME'S SAVE |
| `tools/title_route.py --title spyro2` | reached gameplay after 3340 fields (Glimmer), exit 0 |
| `tools/title_route.py --title spyro3` | reached gameplay after 4890 fields, exit 0 |

`drive.py gameplay` is qualified because it looks like a regression here and is not. On THIS
worktree's shared `scratch/saves/card.mcr` - the blank image, digest `77d33c6b` - it refuses
DETERMINISTICALLY (repeated runs, always frames 1740/1760) at the create-save prompt, which is
upstream's recorded behaviour for Spyro 1 on a blank card. Given a card holding the game's save it
reaches Artisans at 6360 on this same binary. So the code is unchanged and the card is the variable -
and it is the same variable this issue is about, now visible in the driver as well as in the pad file.
The record/replay session is inert with no record or replay path configured
(`PadRecordReplay::service` returns the live mask when nothing is configured, and Spyro's
`InputPhase::of` only reads two guest words), so none of the pad work can move that number.

The two title routes were re-measured on the blank `drive.ROUTE_CARD` after this change, and both
match what docs/issues/0169 recorded for a blank card (3340 and 4890), which is the point: the pad
work moved nothing in the routes that carry no recording.
