---
id: C028
kind: claim
status: holds
created: 2026-07-28
tags: recomp
---

## Claim

Recompiling the OVL0 overlay unblocked the boot: frames 436 -> 3781 and the gate is fully green

## Evidence

Sliced WAD.WAD +0x5B800 (14336 bytes) to an overlay .BIN, declared base 0x8007AA38 in game/recomp_seeds.json, wired extraction into tools/ensure_recomp.py, re-emitted (6 overlay functions; resident set 621 -> 629). One further miss surfaced at 0x80024054 (reached by fn-pointer from ra=0x80078A58) and was seeded with that rationale. Result: tools/gate.sh passes ALL SEVEN checks — 3781 frames, 18 distinct occupancies, 6 loader calls, 628736 bytes read, 18 completions, ZERO recomp misses, zero refused registrations. Frames were 436 before the overlay work.

## What would falsify it

if a longer run surfaces new recomp misses, the overlay set is incomplete — the decomps describe 37 overlays and only one is located so far
