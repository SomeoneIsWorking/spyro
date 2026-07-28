---
id: C027
kind: claim
status: holds
created: 2026-07-28
tags: recomp
---

## Claim

Overlay LOCATED: WAD.WAD +0x5B800, 14336 bytes, loads to 0x8007AA38 and is genuine MIPS code

## Evidence

The loader's third call is dest=0x8007AA38 len=14336 a3=0x5B800, and that span covers ALL FOUR hardcoded call targets (0x8007AA50 +0x18, 0x8007ABAC +0x174, 0x8007BFD0 +0x1598, 0x8007CEE4 +0x24AC). Decoding those bytes straight from the extracted WAD.WAD at base 0x8007AA38: 3583 words, 100.0% valid opcodes, with a real-code distribution (nop 738, addiu 619, lui 470, lw 350, addu 214, sw 210, jal 189, bne 184). Call targets land on prologue shapes — 0x8007CEE4 = addiu/sw/lui/addiu, 0x8007ABAC = lui/lw/addiu/sw. This is STATIC evidence from the disc, so unlike the earlier memory sampling it does not depend on when the region was observed.

## What would falsify it

if recompiling this region and running it produces immediate dispatch misses at addresses inside it, the base or extent is wrong
