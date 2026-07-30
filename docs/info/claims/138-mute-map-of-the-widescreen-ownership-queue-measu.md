---
id: C138
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,ownership
---

## Claim

MUTE MAP of the widescreen ownership queue, measured not inferred: 0x80020F34 draws the GROUND and cliffs (muting it leaves the world empty against the backdrop); 0x80022A2C draws a foreground object plus the DEMO MODE caption; 0x8001F798 and 0x800258F0 both remove EXACTLY the orange character and their muted frames are BYTE-IDENTICAL to each other, so they are two stages of one actor pipeline rather than two independent renderers — and 0x8001F798 contains no jal, so it is not simply calling the other. Together with the already-owned 0x8004EBA8 (sky + distant terrain, C135) that accounts for every visible element of the frame.

## Evidence

Five captures of the SAME frame (46501, DEMO MODE gameplay) via tools/shot.py, one per muted body, using the new PSXPORT_MUTE_FN: scratch/screenshots/mute_none.png, mute_80022A2C.png, mute_80020F34.png, mute_8001F798.png, mute_800258F0.png. Compared by eye and by md5.

## What would falsify it

a different scene where a muted body removes different content — this is one frame of one level, and a renderer with several roles could look single-purpose here
