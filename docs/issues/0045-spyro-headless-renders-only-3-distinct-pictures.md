---
id: 45
title: spyro flickering graphics (USER-REPORTED, still OPEN) — the "headless renders only ~3 pictures then stalls" premise is REFUTED; there is NO headless/windowed divergence
status: open
symptom: USER, windowed: "spyro has flickering graphics". (The headless "frozen picture" that was recorded here on 2026-08-05 was an INSTRUMENT ARTEFACT, not a symptom — see below.)
tags: render,flicker,user-reported,corrected,instrument-artefact
created: 2026-08-05
updated: 2026-08-06
---

> **CORRECTED 2026-08-06. Both halves of this entry's original premise were false, and it is rewritten
> rather than appended to, per the project rule that a note is FIXED, not contradicted-and-left-standing.**
> The falsified reading is not preserved as prose — what is preserved is every NUMBER that was actually
> measured, re-attached to what it actually shows. The filename slug still carries the old wording; it
> is kept only for link stability (the id in the frontmatter is what `catalog.py` indexes).

## The live bug

USER-REPORTED 2026-08-05, in a WINDOWED run: **"spyro has flickering graphics."** That is what this
entry tracks and it is OPEN. Nothing below closes it.

## What was REFUTED, and why it matters

This entry previously told every future session two things. Both are false.

**FALSE #1 — "headless renders only ~3 distinct pictures then stalls; windowed advances."**
The dumps were frozen; the GAME was not. `PSXPORT_GPU_DUMP` reads `s_vram`, the SOFTWARE VRAM array.
Under `vk_path()` — the default; `soft_gpu` is only set in SBS oracle mode — every polygon and sprite
goes to the VK rasterizer and **never touches `s_vram`**. Only VRAM uploads and fills land there.
`gpu_native_shot` says so in its own words: *"VK render lives in the GPU image, not s_vram."*
So on a game whose front end is uploads and whose gameplay is geometry, this dump shows the front end
and then goes permanently still the moment real rendering begins. That is exactly the observed shape.
This is **instrument I008**, and it was ALREADY registered in `docs/info/instruments.md` (as
`008-psxport-gpu-dump-ppm-frame-dump-blind-to-vk-rend.md`) when this entry was written — the registry
had the answer and was not consulted. (I008's frontmatter also still read `status: trusted` while its
body said DISTRUSTED, so `info.py instrument list` printed `✓ I008 [trusted]`; that is fixed in the
same change as this correction.)

**FALSE #2 — "the run then dies on the watchdog 3s frame-progress timeout."**
It does not. The run dies on the `rec_dispatch_miss` SIGABRT at frame 3544 that is
**issue 0046** — psxport's `ra_computed_jumps` misclassifying the tail of `0x80022A2C` as a coroutine
resume, so `rec_dispatch(c, 0x8001E91C)` misses and aborts. Both are "signal 6", which is how the two
got conflated. 0046 is the real death and the real blocker; it is a FRAMEWORK defect in
`external/psxport/tools/recomp/emit.py` and it is why `tools/gate.sh` cannot pass either.

**THEREFORE: there is NO headless/windowed behavioural divergence in this port.** MEASURED by the
2026-08-06 triage: headless and windowed both run **3545 presents and abort identically**. The
project's "headless and windowed are one code path" rule was never violated here, and the "close the
divergence before you can chase the flicker" instruction this entry issued was a block on work that
had nothing to block it.

## The corrected record of what was actually measured

Every number below was really produced; only the conclusion drawn from it was wrong.

| measured 2026-08-05 | what it actually shows |
|---|---|
| headless, no input, `PSXPORT_GPU_DUMP` every frame: 3545 frames dumped, 3 of 141 widely-spaced consecutive pairs differ | `s_vram` changed 3 times. Says nothing about VK-rasterised geometry, which is where the whole 3D phase lives. |
| a contiguous 40-frame window deep in the run is bit-identical (0.00% pixels changed, nonblack steady 93.33%) | that window is deep in the geometry phase, i.e. exactly where I008 is blind. Stillness is the instrument's floor, not a reading. |
| USER'S OWN PAD replayed (`replays/bugs/flicker-session.pad`): 42264 frames, 3 of 211 pairs differ | the pad replay works and the run is long; the dump is blind in both runs, so real input could not have moved this number either. |
| "the run dies at signal 06" | true, and it is issue 0046's `rec_dispatch_miss` abort at f3544, not the watchdog. |

The pad capture itself is good evidence and is unaffected: `replays/bugs/flicker-session.pad` is a
real recording of the session in which the user saw the flicker, and it is still the right input for
reproducing it.

## The method failure, recorded because it is the reusable part

The original entry contained a paragraph headed *"THE INSTRUMENT IS VALIDATED, so 'frozen' is an
observation and not a broken dump"*, and that paragraph is why the wrong reading survived. Its
positive control was **f00025 with 40.48% of pixels changed** — a frame in the UPLOAD-driven front
end, which is precisely the class I008 CAN see. It was then generalised to the geometry phase, which
I008 CANNOT see. A discriminator was run against one class and its result claimed for both.

The rule this violates is already in the house rules: *a discriminator must be run against BOTH
classes before you trust it*. The second half of the "validation" — that the dump follows the display
origin (`gpu_native.cpp` dumps at `s_disp_x/s_disp_y`, written by GP1(05)) — is TRUE and is also
irrelevant: reading the right REGION of the wrong BUFFER is still the wrong buffer.

## What to use instead

Not `PSXPORT_GPU_DUMP`, for any question of the form "is the game still drawing" or "what does the
picture look like". Use a present-stage capture (`PSXPORT_SHOT_AT` / `gpu_vk_shot`, I033) for the
picture, and the guest's own prim-submission count for liveness — I008's own entry records that 680
frames in the last quarter of that run submitted prims, which already contradicted "the game stopped
drawing" at the time.

**And write per-run capture directories.** `scratch/screenshots/` is a SHARED ACCUMULATOR written by
every run in this repo; globbing it sweeps in stale files from earlier runs. That has already
manufactured a false root cause in the sibling port, and I036 records the same failure happening here
(three of five captures mislabelled, C138). Verify every file against its own capture log line before
reading it.

## Open leads on the actual flicker — NOT VERIFIED HERE

Recorded as leads, explicitly not as findings. This correction re-ran nothing; it verified I008's
mechanism, verified issue 0046's abort, and took the headless-vs-windowed equality from the
2026-08-06 triage.

* The 2026-08-06 triage reports that the composite has no PERSISTENCE: `upload_vram()` memcpys all
  guest CPU VRAM over `s_vram_tex` every present, `render_geom()` draws only the current batch, and
  `frame_end()` clears the batch every present — while Spyro is a 30 Hz double-buffered guest
  presenting at 60 Hz. It also reports 3108/3108 presents in the 3D phase are REBUILD_GEOM, so the
  REBUILD_VRAM arm is not involved. **Not re-verified in this entry.**
* Issue 0046 must land regardless: until it does, no spyro run gets past f3544, so any flicker
  measurement is confined to the first ~59 seconds of guest time.

## Cross-references

* **issue 0046** — `ra_computed_jumps` / `rec_dispatch_miss` at f3544. The real death of every run
  described above, and a hard blocker on gate.sh.
* **I008** — `PSXPORT_GPU_DUMP` is blind to VK-rendered geometry. The instrument that produced the
  false premise.
* **I033 / I036** — the present-stage capture, and the shared-accumulator hazard in
  `scratch/screenshots/`.
* `replays/bugs/flicker-session.pad` — the user's own session input, still valid.
