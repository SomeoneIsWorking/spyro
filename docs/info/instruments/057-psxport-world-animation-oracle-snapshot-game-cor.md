---
id: I057
kind: instrument
status: trusted
created: 2026-08-28
---

## Instrument

PSXPORT_WORLD_ANIMATION_ORACLE / _SNAPSHOT (game/core/world_animation_oracle.cpp) — compares the native phase-1 world animation against the retained gen_func_800258F0 by running that body twice from identical RAM (guest leg: animate+render; native leg: native animation, then render-only because the channels are already retired) and requiring byte-identical guest RAM. _SNAPSHOT=<snap.bin>[,<selection>] runs the same A/B against a captured frame, which is the only way to reach the FIELD since the reference leg fail-fasts at the title's guest-VSync tail.

## Validated by

Shown to report BOTH answers on the same input: PASS 'IDENTICAL guest RAM' on scratch/raw/stage0_artisans_refusal.bin, and with PSXPORT_WORLD_ANIMATION_ORACLE_MUTATE=1 (a shipped negative control that flips one byte the animation itself wrote) '80 byte(s) of guest RAM DIFFER, first at 0x800B9EBC'. A single flipped animation byte propagates into the packet stream, so the comparison is sensitive to what it claims to measure. It also refuses loudly when a run reaches zero comparable calls, and when every compared call had zero live channels.

## Known failure modes

(none recorded yet)
