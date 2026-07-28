---
id: C063
kind: claim
status: holds
created: 2026-07-28
tags: input,pad,vsync
---

## Claim

Spyro's REAL pad decoder (0x80053C68) is registered as the VBLANK CALLBACK via VSyncCallback(0x8005DE58), so in a no-IRQ runtime it ran exactly ONCE at boot and never again — that, not a missing pad buffer, is why live input never reached the game and the port looped attract forever.

## Evidence

Boot input setup 0x800123C8 does, in order: jal 0x8006B010 PadInitDirect(0x800786A0,0x80078E50); jal 0x80053C68 (prime); jal 0x8005DE58 VSyncCallback(0x80053C68). 0x8005DE58 loads [0x800749AC]+20 and jalr's it with a0=4 (the VSYNC slot). Probe on 0x80053C68 in a 20s headless run logged 'call #1' and nothing else. After running the registered callback once per vblank from vsync.cpp, the SAME probe logs 4106+ calls and the pad-class word [0x80077384] moves 0 -> 2 (digital).

## What would falsify it

if a later run shows 0x80053C68 being reached by any path other than the vblank callback, or if [0x80075734]-style indirect dispatch turns out to call it per-frame
