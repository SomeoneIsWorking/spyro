---
id: 24
title: Attract demo replays recorded input; real pad path still unidentified
status: open
symptom: The port completes and loops the attract sequence (stage mode 13 -> 0 -> 13, overlays swapping) but never enters the level-load arm, because nothing ever presses START.
tags: input,stage
created: 2026-07-28
updated: 2026-07-28
---

See C062. This reframes the input question entirely and supersedes the framing in
docs/re-frontier.md's input.pad entry.

WHAT THE PORT DOES NOW. It completes the attract sequence and loops it: stage mode
[0x800757D8] cycles 13 -> 0 -> 13, overlays swap OVL0 -> OVL1 -> OVL0 through the shared arena, and
the level-load arm (modes 4/5) is entered zero times. That is a title/attract loop behaving correctly,
not a stall.

WHY THE GAME NEVER READS A PAD. During attract it replays a RECORDED input stream:
    0x80053A90  lw   v1, [0x8007585C]      ; pointer
    0x80053A98  addiu v0, v1, 4
    0x80053AA0  sw   v0, [0x8007585C]      ; advance one word per call
    0x80053AA4  lw   s0, 0(v1)             ; the "button word" for this frame
s0 then feeds the ordinary edge-detect into [0x80077378]/[0x80077380]. So the replayed word is
consumed exactly as live pad state would be.

This is consistent with, not contrary to, C035: zero JOY-register accesses and a dead libapi pad chain
across the resident text and all 36 code overlays (re-confirmed including the previously-excluded
0x8006C000-0x80073000 region).

OPEN QUESTION, and the right next one: where does the game read REAL pad state — the START press that
would break out of attract? Candidates in order:
  1. The same [0x80077378]/[0x80077380] pair, written by a DIFFERENT producer than the demo replay.
     0x80053AF8 and 0x8005413C both write 0x80077378 — find what feeds 0x8005413C.
  2. A BIOS-filled buffer the game polls, which is what GameConfig's pad group exists to describe.
Do NOT wire a pad buffer address until the producer is identified. The demo path proves the game can
be fed input without touching hardware, so a plausible-looking buffer is easy to guess and wrong.
