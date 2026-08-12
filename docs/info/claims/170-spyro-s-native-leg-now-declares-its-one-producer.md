---
id: C170
kind: claim
status: holds
created: 2026-08-12
tags: producers
depends: game/render/fx_title_menu.cpp#spriteEmit, game/core/frame_loop.cpp
reconfirmed: 2026-08-12 21:16:05
verified_at: 2026-08-12 21:16:05
---

## Claim

Spyro's native leg now DECLARES its one producer to the graphics-producer DB: a capped native-leg run reports 1 row keyed guest 0x8007CD38 'titlefx:spriteEmit' with prims > 0, `first_frame == 585`, `unscoped-native 0`, and a total that agrees EXACTLY with the producer's own `titlefx emitted=` arithmetic at 2 prims per drawing frame — while the same command on the reference leg reports 0 rows with every prim guest-origin, so the row is evidence about the native producer and not a counter that always fires. **THE TOTAL IS NOT PART OF THE CLAIM** (it was, and that was wrong): 1374, 1378 and 1380 prims / 694, 696 and 697 frames have all been observed from one unchanged workload, because `PSXPORT_NATIVE_FRAMES` caps PRESENTED fields while the row counts LOGIC frames. Assert on the three invariants, never on a count.

## Evidence

scratch/logs/prod59_run3.log (native leg, PSXPORT_SPYRO_FRAME_LOOP=1, PSXPORT_NATIVE_FRAMES=3000, headless under gpuguard, rc=0) lines '[producers] run-end: 1 row(s); prims seen 1380 = attributed 1380 + unscoped-native 0 + guest-origin 0' and the row line. Cross-checked against an INDEPENDENT counter in the producer itself (PSXPORT_DEBUG=titlefx 'emitted=' from spriteEmit's return value): 1282 calls = 585x0 + 14x1 + 683x2 = 1380 prims over 697 frames, and the first non-zero emit is call #585 == the row's first_frame f585. Negative control scratch/logs/prod59_neg.log (PSXPORT_RENDER_PATH=gte, same cap): 0 row(s), guest-origin 1481492. The scope is keyed on the guest submitter spriteEmit transcribes, NOT on the arm 0x8007CEE4 nor the shared AddPrim leaf 0x800168DC/0x80016784 (producer_scope.h's rule; reasons in issue #59).

## The numbers vary at the cap boundary — the OBSERVED RANGE IS NOT A BOUND, and this section said otherwise

CORRECTED 2026-08-12 by an independent verification pass. The heading below used to read "read them as 1378-1380 / 696-697" and the body called that a measured spread; a fresh build against the pin the port actually records (`external/psxport` = `726d10c9`, binary md5 `303763bdb3658e6b0dd74f29a9da4c34`) then landed on **`frames 694 (f585..f1279)` / 1374 prims** (`scratch/logs/V_native3000.log`, cap 3000, `gpuguard run`, rc=0) — outside it. Writing an observed range down as a range invites the next reader to assert on its edges, which is the same error as asserting on a total. **The claim's content is the CROSS-CHECK, not the numbers:** the row must agree exactly with the producer's own arithmetic (here 585 non-drawing + 14 one-prim + 680 two-prim = 694 drawing frames, 14 + 1360 = 1374 prims — exact), and `first_frame` must be 585 (now eight runs, three builds, two pins). Totals seen in one session: 1374, 1378, 1380.

## Original note, kept for the reasoning (its stated range is superseded above)

Measured 2026-08-12 over three runs of the identical command: 1380 prims / 697 frames (f585..f1282) twice, and 1378 / 696 (f585..f1281) once (scratch/logs/prod_restored.log). The cap is `PSXPORT_NATIVE_FRAMES=3000` PRESENTED frames, so where it lands relative to a logic frame decides whether the last frame's two prims are counted. In every run the row agrees EXACTLY with the producer's independent `titlefx` counter (585x0 + 14x1 + 682x2 = 1378 / 696 drawing frames in that run; 683x2 = 1380 / 697 in the others), which is the cross-check that matters — a mismatch between the two counters would falsify this claim, a one-frame difference in the cap boundary would not.

## What would falsify it

a native-leg capped run that reports 'NEVER FED', 0 rows, or a nonzero unscoped-native; a run whose census total disagrees with the sum of titlefx 'emitted='; or a reference-leg run that also reports a 0x8007CD38 native row (which would mean the row is not evidence about the native producer). NOTE it does NOT claim the two legs are joined: #56 keeps packetPoolBase 0, so the row's 'guest 0' is 'not measured'.

## Re-confirmed 2026-08-12 21:16:05

REPRODUCED by an independent verification pass 2026-08-12 on a FRESH build against the pin the port actually records (external/psxport=726d10c9, binary md5 303763bdb3658e6b0dd74f29a9da4c34 — not the 7c503939 the original evidence quotes, because the gitlink moved in 316f90e). scratch/logs/V_native3000.log (cap 3000, gpuguard run --timeout 300, rc=0): '1 row(s); prims seen 1374 = attributed 1374 + unscoped-native 0 + guest-origin 0 + gp0-anon 0 + span-miss 0 + span-no-fn 0' / 'guest 0x8007CD38 native 1374 guest 0 frames 694 (f585..f1279) titlefx:spriteEmit', and ZERO UNDECLARED lines. Negative control reproduced too: reference leg same cap (scratch/logs/V_refleg3000.log, rc=0) gives 0 rows, guest-origin 1368336, so the row is evidence about the NATIVE producer. CORRECTION LANDED: the claim's stated 1378-1380/696-697 band is NOT a bound — 694/1374 falls outside it. The invariants that DID hold are first_frame==585 and the exact agreement with the producer arithmetic (585 non-drawing + 14 one-prim + 680 two-prim = 694 drawing frames, 1374 prims).
