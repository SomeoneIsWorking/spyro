---
id: C110
kind: claim
status: holds
created: 2026-07-29
tags: input,pad
---

## Claim

Spyro has two distinct pad globals: [0x80077380] is the HELD word and [0x80077378] is the 'newly pressed' EDGE word, non-zero for two frames per press. Both are written every frame by pc=0x8006B64C ra=0x80053D50. The title handler uses BOTH, at different points: its head tests the HELD word (0x8007AC48, gated on [0x80078D88]==2 and the title counter >= 300) to slam the counter to 1170, while the sub-state-1 arm tests the EDGE word (0x8007B88C, mask 0x840 = START bit 11 or X bit 6). An input mechanism that supplies only one shape can therefore drive one transition and not the other.

## Evidence

PSXPORT_WWATCH on 0x80077378 over a run with a REPL press: 241180 stores, exactly three non-zero — 0xFFFF0000 at f436 (pad init) and 0x00000800 at f1301/f1302, the two frames of one edge — while [0x80077380] reads 0xFFFF0800 continuously across the same span. The two use sites were read from the resident OV_5B800 image (residency confirmed 256/256 against a title-screen RAM dump). Corrects C109, which claimed FORCE_BUTTONS produces no edges; it pulses 8-on/24-off precisely to produce them.

## What would falsify it

A run where [0x80077378] is non-zero for more than a couple of consecutive frames while a button is held (would mean it is not edge-shaped), or where the two addresses hold equal values across a press.
