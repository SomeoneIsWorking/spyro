---
id: C064
kind: claim
status: holds
created: 2026-07-28
tags: input,pad,instrument
---

## Claim

Spyro DOES access the JOY/SIO0 registers — through an initialised POINTER ([0x80075220] = 0x1F801040), not immediate addressing. C035's 'zero JOY-register accesses' was an artifact of an incomplete address scan.

## Evidence

0x8006993C lw v1,[0x80075220]; 0x8006994C sh 0x0040,10(v1) (JOY_CTRL reset); 0x80069958 sh 0x000D,8(v1) (JOY_MODE); 0x80069960 sh 0x0088,14(v1) (JOY_BAUD); 0x800699A0 sh 0x1003/0x3003,10(v1). exe.word(0x80075220) reads 0x1F801040 straight out of the image, and no instruction stores to 0x80075220, so it is initialised data. Spyro links Sony's libpad direct-SIO driver at ~0x80069000-0x8006C000.

## What would falsify it

if a disassembly of 0x80069000-0x8006C000 shows those sh/lw offsets targeting something other than SIO0, i.e. [0x80075220] is repointed at runtime by code the scan missed
