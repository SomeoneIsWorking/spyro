---
id: C062
kind: claim
status: holds
created: 2026-07-28
tags: input,stage
---

## Claim

The attract demo replays RECORDED INPUT from a walking pointer — which is why the game never touches the pad, and why the port cycles 13->0->13

## Evidence

At 0x80053A90-0x80053AA4: v1 = [0x8007585C]; v0 = v1+4; [0x8007585C] = v0; s0 = [v1]. So 0x8007585C holds a POINTER that advances one word per call, and the current button word is read THROUGH it. s0 then feeds the standard edge-detect at 0x80053AC8-0x80053AF8 (v1 = [0x80077380] previous, v0 = ~v1 & s0 newly-pressed -> [0x80077378]), so the replayed word is treated exactly as live pad state would be. That reconciles C035 (zero JOY-register accesses and a dead libapi pad chain anywhere in resident text OR all 36 code overlays, re-confirmed this session including the previously-excluded 0x8006C000-0x80073000 region) with a game that obviously responds to input: during ATTRACT it is not reading a pad at all, it is playing back a recorded stream. Runtime confirms the loop completes: stage mode cycles 13 -> 0 -> 13 and overlays swap OVL0 -> OVL1 -> OVL0, with the level-load setup entered zero times.

## What would falsify it

The pointer at 0x8007585C not advancing across frames, or the buffer it walks containing something other than button-shaped words.
