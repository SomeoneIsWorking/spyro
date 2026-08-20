---
id: C207
kind: claim
status: holds
created: 2026-08-20
tags: [native, ownership]
depends: game/core/native_printf.cpp#write_printf_native, game/core/game_hooks.cpp#spyro_registerOverrides
---

## Claim

Spyro owns its first exercised non-leaf body: the PsyQ printf wrapper at 0x8006279C

## Evidence

SCUS_942.28 bytes 0x52F9C..0x52FD7 disassemble to the 15-instruction vararg-homing wrapper and 40 direct jal xrefs reach it. A pre-override FNTRACE run reached it once at frame 0 from ra=0x8005F314. After the Clang build, PSXPORT_NDIFF=1 reported printf@0x8006279C call #1 matches the recompiled body exactly across its compared state; tools/own_candidates.py derives 21 owned ndiff sites. The retained child dispatch at 0x800627D8 and generated wrapper remain compiled.

## What would falsify it

The executable bytes or generated wrapper change, the override no longer fires on startup, or PSXPORT_NDIFF reports any divergence for an exercised input.
