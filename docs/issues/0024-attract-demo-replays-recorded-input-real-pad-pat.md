---
id: 24
title: Attract demo replays recorded input; real pad path still unidentified
status: resolved
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

### Resolution (2026-07-28)
The real pad path was never a buffer to find — it was a CALLBACK THAT NEVER FIRED. Boot at 0x800123C8 calls PadInitDirect(0x800786A0, 0x80078E50), primes the decoder once, then hands 0x80053C68 to VSyncCallback (0x8005DE58): the decoder is the VBLANK IRQ handler. This runtime raises no IRQs, so it ran exactly once at boot (probe: 'call #1' and nothing else in a 20s run) and the only thing left publishing input was the attract demo's playback path. Two fixes, both in game/core/vsync.cpp's vblank wait, which is this port's frame boundary: (1) Pad::serviceFrame() writes the standard PSX packet into the slot buffers that libpad's SIO read would have filled; (2) the registered vblank callback is then run, with the register file saved and restored the way an IRQ would. Measured after: pad class [0x80077384] 0 -> 2 (digital), decoder calls 1 -> 4106+, and the game leaves attract and loads a level (bytes-from-disc 4.9 MB -> 9.9 MB, a third overlay identified). Also falsified C035 en route: the JOY/SIO accesses were always present, reached through the initialised pointer [0x80075220] = 0x1F801040 — the old scanner could only see lui/addiu-built addresses, so 'zero accesses' was the instrument's blind spot, not a fact about the game (C064).
