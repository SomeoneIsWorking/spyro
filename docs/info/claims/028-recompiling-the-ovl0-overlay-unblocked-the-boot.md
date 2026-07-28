---
id: C028
kind: claim
status: falsified
created: 2026-07-28
tags: recomp
falsified_on: 2026-07-28
---

## Claim

Recompiling the OVL0 overlay unblocked the boot: frames 436 -> 3781 and the gate is fully green

## Evidence

Sliced WAD.WAD +0x5B800 (14336 bytes) to an overlay .BIN, declared base 0x8007AA38 in game/recomp_seeds.json, wired extraction into tools/ensure_recomp.py, re-emitted (6 overlay functions; resident set 621 -> 629). One further miss surfaced at 0x80024054 (reached by fn-pointer from ra=0x80078A58) and was seeded with that rationale. Result: tools/gate.sh passes ALL SEVEN checks — 3781 frames, 18 distinct occupancies, 6 loader calls, 628736 bytes read, 18 completions, ZERO recomp misses, zero refused registrations. Frames were 436 before the overlay work.

## What would falsify it

if a longer run surfaces new recomp misses, the overlay set is incomplete — the decomps describe 37 overlays and only one is located so far

## FALSIFIED 2026-07-28

The direction is right but the headline number is wrong in a way that matters. '3781 frames' is not a duration the port survives — it is the frame the port CRASHES on. Proven by running with a 20s and a 70s timeout and getting identically 3781 frames both times: the count is not time-bound at all. The port aborts in the framework's own fail-fast, '[rq:error] FATAL: render queue full (65536 items) — refusing to drop prims', reached through gpu_dma2_linked_list <- io_write <- gen_func_80061820 <- gen_func_8005FD64 <- gen_func_8001E6B8 <- gen_func_8001ED5C <- main. 'the gate is fully green' was also false: the gate ran the port under 'timeout -s KILL', which swallows the exit status, so a crashing port looked identical to a healthy one (instrument I007). Recompiling OVL0 DID advance the boot past its previous stall — that part stands — but it advanced it to a crash, not to a running game. Superseded by C036.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
