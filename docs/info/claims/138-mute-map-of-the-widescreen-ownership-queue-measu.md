---
id: C138
kind: claim
status: falsified
created: 2026-07-30
tags: render,widescreen,ownership
falsified_on: 2026-07-30
---

## Claim

MUTE MAP of the widescreen ownership queue, measured not inferred: 0x80020F34 draws the GROUND and cliffs (muting it leaves the world empty against the backdrop); 0x80022A2C draws a foreground object plus the DEMO MODE caption; 0x8001F798 and 0x800258F0 both remove EXACTLY the orange character and their muted frames are BYTE-IDENTICAL to each other, so they are two stages of one actor pipeline rather than two independent renderers — and 0x8001F798 contains no jal, so it is not simply calling the other. Together with the already-owned 0x8004EBA8 (sky + distant terrain, C135) that accounts for every visible element of the frame.

## Evidence

Five captures of the SAME frame (46501, DEMO MODE gameplay) via tools/shot.py, one per muted body, using the new PSXPORT_MUTE_FN: scratch/screenshots/mute_none.png, mute_80022A2C.png, mute_80020F34.png, mute_8001F798.png, mute_800258F0.png. Compared by eye and by md5.

## What would falsify it

a different scene where a muted body removes different content — this is one frame of one level, and a renderer with several roles could look single-purpose here

## FALSIFIED 2026-07-30

STALE CAPTURE FILE, not a fact about the engine. tools/shot.py always writes scratch/screenshots/f46501.png, and the mute captures were made by running it then copying that file — so a run that produced no new image left the PREVIOUS one in place and the copy was silently mislabelled. Re-run with the output file deleted before each run, three of the five entries change: 0x800258F0 removes the GROUND and cliffs (not the orange character), 0x80020F34 removes a SECOND character in white/blue (not the ground), and 0x8001F798 removes the orange character (unchanged). The 'byte-identical frames from muting 0x8001F798 and 0x800258F0' that C138 recorded as evidence of one two-stage pipeline was simply the same file twice — and it was flagged as unexplained at the time, which should have been read as an instrument failure rather than a finding. Superseded by C147; the corrected map agrees with every open-spyro symbol name.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
