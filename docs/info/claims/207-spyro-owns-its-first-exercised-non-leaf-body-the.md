---
id: C207
kind: claim
status: holds
created: 2026-08-20
tags: [native, ownership]
depends: game/core/native_printf.cpp#write_printf_native, game/core/game_hooks.cpp#spyro_registerOverrides
reconfirmed: 2026-08-21 01:11:23
verified_at: 2026-08-21 01:11:23
---

## Claim

Spyro owns its first exercised non-leaf body: the PsyQ printf wrapper at 0x8006279C

## Evidence

SCUS_942.28 bytes 0x52F9C..0x52FD7 disassemble to the 15-instruction vararg-homing wrapper and 40 direct jal xrefs reach it. A pre-override FNTRACE run reached it once at frame 0 from ra=0x8005F314. After the Clang build, PSXPORT_NDIFF=1 reported printf@0x8006279C call #1 matches the recompiled body exactly across its compared state; tools/own_candidates.py derives 21 owned ndiff sites. The retained child dispatch at 0x800627D8 and generated wrapper remain compiled.

## What would falsify it

The executable bytes or generated wrapper change, the override no longer fires on startup, or PSXPORT_NDIFF reports any divergence for an exercised input.

## Re-confirmed 2026-08-21 01:11:23

Reconfirmed against the rebuilt Clang binary and shared psxport 81cb8e05-dirty: scratch/logs/gate-boot-20260821-011226.log line 59 reports '[ndiff] printf@0x8006279C call #1 matches the recompiled body exactly'. The run exited cleanly at its 3,000-field cap. tools/own_candidates.py independently reports the executable extent 0x8006279C..0x800627D8 as 15 instructions/non-leaf with 40 static callers, and tools/xrefs.py enumerates all 40 jal sites.
