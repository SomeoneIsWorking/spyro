---
id: C173
kind: claim
status: holds
created: 2026-08-12
tags: producers,census,frame-loop
depends: titles/spyro1/core/spyro1_frame_driver.cpp
---

## Claim

Spyro's frame loop now drives the framework's FULL per-logic-frame contract — both Timing::logicFrame AND OtAttr::beginLogicFrame — and the second half is DORMANT today for a reason that was measured, not assumed: packetPoolBase/Stride are 0 (#56) so the span table takes zero entries, which means #56 alone is the sufficient cause of the reference leg's 100% span-miss and the missing reset was NOT a second cause of it

## Evidence

MEASURED 2026-08-12, pin external/psxport=726d10c9. THE GAP: 'grep -rn beginLogicFrame game/' returned 0, and OtAttr::resetIfNewFrame (ot_attr.cpp:46) is reached from beginLogicFrame and NO other caller, so this port drove the span-table reset not at all — while the framework loop does BOTH (native_boot.cpp:112 sets logicFrame, :384 calls beginLogicFrame). Same root cause as #60's counter. THE DORMANCY IS PROVEN, NOT ASSUMED: trackStoreSlow records a span only inside the packet pool ('if (!pool.known || k < pool.lo || k >= pool.hi) return;'), and pool_range returns known=false because game/core/game_config.cpp:79-80 set packetPoolBase/Stride to 0 — the framework prints its own confirmation, 'otattr:warn GameConfig::packetPoolBase/Stride are 0 for this game — packet-pool attribution is STRUCTURALLY BLIND here'. Reference-leg run AFTER the change (scratch/logs/V_refleg_after.log, cap 3000, rc=0): 'prims seen 2970452 = attributed 0 + unscoped-native 0 + guest-origin 1485226 + gp0-anon 0 + span-miss 1485226', i.e. span-miss unchanged and still exactly equal to guest-origin (#61's double count, reproduced a third time). Native leg and full gate unaffected: gate rc=0, '19 PASS, 0 FAIL'. WHY LAND IT: it stops being dormant the instant #56 lands — a filled pool would fill the table against a never-advancing stamp with no reset, saturating at SPAN_CAP with every later lookup matching a stale fn==0 span, which is the exact failure ot_attr.h records beginLogicFrame being written to remove (60 logic frames advanced s_frame to 3; 140,153 overflows; guest leg attributed NONE).

## What would falsify it

a reference-leg run whose span-miss changes when the beginLogicFrame call is removed (which would mean the table was NOT empty and #56 was not the sole cause); or psxport reaching a pin past 868aa7e3 where ot_attr.cpp's one-shot 'no frame loop has ever called beginLogicFrame' warning STILL fires on a spyro run (which would mean this call is not wired to the framework's own notion of the contract); or a spyro run recording a nonzero OtAttr spanCount while packetPoolBase is still 0
