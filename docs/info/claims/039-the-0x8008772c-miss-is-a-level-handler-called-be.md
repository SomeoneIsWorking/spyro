---
id: C039
kind: claim
status: holds
created: 2026-07-28
tags: overlay,blocker
---

## Claim

The 0x8008772C miss is a level HANDLER called before its overlay was loaded — and it locates the level overlays at the arena

## Evidence

The bad pointer comes from the global [0x800758CC]: at 0x80014470 the guest does 'lw v0,0x58cc(v0); jalr v0' with a0=0x78. Static scan finds 43 stores to that global in resident text, clustered in one installer at 0x8005A4BC-0x8005A694, each writing a constant built by lui+addiu. Of the 43 installed values, 7 fall INSIDE OVL0's span [0x8007AA38,0x8007E238) and 36 fall outside, spanning 0x80080548 to 0x8008B2C0 — and the missed target 0x8008772C is one of them. 0x8008B2C0 - 0x8007AA38 = 67720 bytes, which sits inside the level overlays' measured size range (38912-81920, claim C033), so all 36 are consistent with ONE larger overlay loaded at the SAME arena base 0x8007AA38 that OVL0 uses. 36 also matches the 35-36 level code entries found in WAD.WAD. So the sequence is: the game selected a level and ran its handler installer, but the level overlay was never loaded — cdq logging shows only six loader calls for the entire run and OVL0 is the only code overlay among them — and then called into memory nothing had written.

## What would falsify it

An observed loader call placing a level overlay somewhere other than 0x8007AA38, or finding that the 36 out-of-OVL0 handlers belong to more than one distinct address region.
