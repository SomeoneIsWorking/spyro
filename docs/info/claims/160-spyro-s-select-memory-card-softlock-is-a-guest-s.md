---
id: C160
kind: claim
status: holds
created: 2026-08-06
tags: memcard,irq,softlock,user-reported
depends: game/core/vsync.cpp#memcard_sync
---

## Claim

Spyro's SELECT MEMORY CARD softlock is a guest spin waiting on an interrupt this port only delivers from vblank_wait: MemCardSync(mode=0) at 0x80067628 spins on [0x80075B58], whose sole writer is the card callback 0x80067CD4 running as libetc VBlank slot 7. Supplying vblanks inside that wait removes the deadlock without reimplementing or faking anything.

## Evidence

Headless, deterministic pad scratch/mc/pads/two_x.pad (the user's replays/bugs/flicker-session.pad + X at f1400 and f1500). UNFIXED build: [watchdog] STUCK, no frame presented, backtrace gen_func_80067628 <- ov_ov_5b800_gen_8007ABAC <- gen_func_8003385C <- gen_func_80012204 (scratch/mc/logs/hang2.log). FIXED build, same pad, same mode: [card] MemCardSync(0): op 3 completed after 2 field(s) and the run proceeds into the card file ops (scratch/mc/logs/fix2.log). Writer identification: PSXPORT_WWATCH=0x80075B58,0x80075B5C PSXPORT_WWATCH_BT=1 logs 280 stores of value 1, ALL pc=80069030 ra=80067CFC with s1=7, zero from any other site (scratch/mc/logs/wwatch.log). Spin located in generated/shard_4.c gen_func_80067628 L_80067684 and selected by the a0==0 test. Regression: 120s no-input headless run logged 0 blocking waits and 0 watchdog trips; tools/gate.sh 90 = 16/16 PASS, 42218 frames, 0 recomp misses, 0 native/substrate divergences over 160 verified calls.

## What would falsify it

0x80075B58 observed being set from anywhere other than 0x80067CD4, or the blocking arm of MemCardSync being entered on a path where delivering vblanks is not what the console would have done (e.g. inside a guest critical section)

## Controlled A/B (the negative control, added after the fact — cite THIS, not the two runs above)

ONE tree, ONE line toggled (`shard_set_override(kMemCardSync, memcard_sync)` present vs replaced by
`(void)`), rebuilt each leg, same pad `scratch/mc/pads/two_x.pad`, same headless env, and each leg
given its own **blank** card image so the two start from identical card state:

| leg | outcome |
|---|---|
| control (override NOT installed) | `[watchdog] STUCK: no frame presented`, backtrace `gen_func_80067628 <- ov_ov_5b800_gen_8007ABAC <- gen_func_8003385C <- gen_func_80012204` (`scratch/mc/logs/abB_control_fresh.log`) |
| fix (override installed) | `[card] MemCardSync(0): op 1 (arg 0x0) completed after 2 field(s), result 0` — so the override demonstrably RAN — and the wedge MOVES to `gen_func_800671F0 <- gen_func_80068FC4 <- gen_func_80067CD4`, the card WRITE op's `B0:0x35 write` retry loop, which is C161 (`scratch/mc/logs/abB_fixed_fresh.log`) |

CAVEAT, measured: on a card that ALREADY holds a `BASCUS-94228SPYRO` save, even the control leg gets
past MemCardSync and wedges in `card_hle_b0` (the READ loop) instead — so C161 is reachable
independently of this fix, and this fix alone does not get the port past the screen in either card
state.

## SUPERSEDED 2026-08-06 — the mechanism holds, the FIX does not, and one premise was false

The RE stands: MemCardSync(mode=0) at 0x80067628 spins on [0x80075B58]; the sole setter is the card
callback 0x80067CD4 in libetc VBlank slot 7; supplying vblanks inside that wait ends the deadlock.

**FALSE PREMISE, corrected.** This claim said "This port raises no interrupts... the spin calls
nothing." The second half is right; the first is not. `tools/recomp/emit.py:1257` emits
`if (c->pending_work) rec_irq_poll(c);` on every loop BACK-EDGE, and it is present in that very spin —
`generated/shard_4.c` L_80067684 carries the gate. The port had no way to *arm* the gate, not no way to
service it: nothing called `rec_host_turn_register`. The title-owned `FieldScheduler` now arms that
class-wide framework mechanism at the native boot/gameplay boundary.

**The override this claim was evidence for is GONE** (`coord/patches-gameside/spyro/REJECTED-memcard-sync-override.diff`):
it was a per-call-site override of ONE address in a class with at least three members, carrying a
`kMaxVblanksPerCardWait = 60` with no ground truth. Cite C163/C164 for the live mechanism.

**And the fix as stated was not sufficient on its own** — running the guest's vblank handler at an
arbitrary point crashes this game, because its renderer keeps scratch values in `$sp`. See C163.
