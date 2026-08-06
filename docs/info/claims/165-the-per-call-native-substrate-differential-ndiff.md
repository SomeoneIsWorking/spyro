---
id: C165
kind: claim
status: holds
created: 2026-08-06
tags: ndiff,hostturn,framework,gate
depends: external/psxport/runtime/recomp/native_diff.cpp#ndiff_run
---

## Claim

The per-call native/substrate differential (ndiff_run) is asymmetric under asynchronous guest execution: only its SUBSTRATE leg contains the pending_work gate, so a host turn can land in one leg and never the other and is reported as the NATIVE body diverging

## Evidence

tools/gate.sh 90 with the host turn armed and no other change: FAIL, 2 native/substrate divergences. The differing bytes name the cause — spin60@0x8005C720 call #8 differed at 0x800749E0 (the libetc vblank counter), 0x80075760 and 0x800758C8 (pad-decoder counters), 0x800773EC..F7 (the pad buffers); fill@0x80016914 differed at 0x8000DFF0, which is inside the vblank handler stack. Every one of those is written by a delivered display field and none is touched by the native bodies. Same tree with rec_host_turn_register removed: 16/16 PASS, 0 divergences, 57511 frames. After making ndiff_run a critical section that rec_host_turn defers across (PW_HOST left armed): 16/16 PASS, 0 divergences, 160 native bodies verified, 53620 frames.

## What would falsify it

a divergence surviving with host turns deferred, or an asynchronous injection shown to reach the native leg as well
