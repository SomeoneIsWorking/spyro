---
id: C109
kind: claim
status: holds
created: 2026-07-29
tags: input,pad
---

## Claim

Spyro has TWO distinct pad globals and the title screen's advance path tests the EDGE one, not the held one. [0x80077380] is the HELD word (0xFFFF0800 continuously while START is down); [0x80077378] is the 'newly pressed' EDGE word, non-zero for exactly two frames per press. Both are written every frame by pc=0x8006B64C ra=0x80053D50. Consequence: PSXPORT_FORCE_BUTTONS, which holds a button from boot, can NEVER satisfy an edge test — so every prior conclusion of the form 'the menu does not want START' drawn from a FORCE_BUTTONS run is unfounded, because the input never reached the branch being tested.

## Evidence

PSXPORT_WWATCH=0x80077378,0x8007737C over a run with REPL 'press start': 241180 stores to that word, of which exactly three are non-zero — 0xFFFF0000 at f436 (pad init) and 0x00000800 at f1301 and f1302, the two frames spanning one press. A REPL read at the same moment shows [0x80077380] = 0xFFFF0800 held continuously. 0x800 is bit 11 = START, matching the game's internal encoding recorded earlier in issue 0027.

## What would falsify it

A run in which PSXPORT_FORCE_BUTTONS produces a non-zero [0x80077378] on any frame after pad init, which would mean the port synthesises edges from a held button and the two words are not distinguishable this way.
