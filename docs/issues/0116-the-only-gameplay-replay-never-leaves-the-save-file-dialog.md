---
id: 116
title: The recorded gameplay replay never leaves the save-file dialog, so every looks-right verdict describes a menu
status: open
symptom: replays/gameplay/artisans-arrival.pad is still on the "YOU WILL NOT BE ABLE TO SAVE YOUR PROGRESS" warning at its last presented fence, while tools/drive.py reaches Artisans on the same build
state_items: S019, S020
tags: replay, input, gameplay, fps60, widescreen, verification
created: 2026-09-19
updated: 2026-09-19
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

## What is not yet known

Whether the recorded pad stream desynchronised — its presses now landing on fences that no longer
expect them, which follows any timing change since it was recorded — or whether the dialog's input
handling regressed. Those are distinguishable: replay the pad against a build at the revision it was
recorded at, or log the pad edges the dialog receives against the ones the file holds.

The durable fix is probably not a re-recorded pad, which would rot the same way. drive.py exists
because "every scripted route in this project so far was a fixed list of run N / tap lines. That
works exactly once." Giving `looks_right` an observed route rather than a recorded one would make
its verdicts about gameplay and keep them that way.
