---
id: C150
kind: claim
status: holds
created: 2026-08-04
tags: frame,re
depends: game/core/wide_clip.cpp#emit_report
---

## Claim

0x8001ED5C IS Spyro's per-frame render driver: it calls every renderer, resets the packet-pool working pointer at 0x800757B0, and flips the draw env. The OT/pool BASES (0x800785E8..F8 and the draw-env fields at env+112/116/120) are written ONCE at boot and never again, so they are static bases, not per-frame state — the only per-frame pointer is 0x800757B0.

## Evidence

PSXPORT_WWATCH on the draw0 env fields 0x80076F50-0x80076F5C over a 30s headless run: 3 stores total, all at frame 436 from pc=0x8005B6F8 (ra=0x8005B8C0), storing 80187BB0/801BFBB8/801BFBB0 — the values C073 records — and ZERO stores thereafter. tools/whowrites.py 0x800785E8 0x800785F8 --after 1000 --secs 60: 8 stores seen, 0 in frames [1000,end), i.e. boot-only, tool explicitly reported the hits-exist-but-outside-window case rather than a bare zero. tools/whowrites.py 0x800757B0 --after 2000 --secs 60: 119984 stores, 116453 in-window, innermost-writer histogram gen_func_80022A2C 64371 / gen_func_8001ED5C 12526 / 8001F798 6072 / 800258F0 6071 / 8004EBA8 6071 / 80019698 5073 / 80024054 4050 / 80020F34 2898, with the example call chain 8001F798 <- 8001ED5C <- 80012204 showing 8001ED5C is the renderers' caller. 8001ED5C's own ~12526 stores over ~6000 frames is ~2/frame, the reset cadence.

## What would falsify it

if a later capture shows a store to 0x800785E8-F8 or to a draw-env +112/116/120 field during gameplay, the bases are not static and the per-frame model is wrong
