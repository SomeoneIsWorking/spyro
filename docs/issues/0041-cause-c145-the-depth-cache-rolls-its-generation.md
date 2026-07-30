---
id: 41
title: CAUSE (C145): the depth cache rolls its generation once per FRAME, but a generation is meant to be one POOL BUFFER. This game fills a buffer every OTHER frame, so an empty frame burns a generation without a flip and the buffer filled two frames ago is retired before the DMA draws it.

RULED OUT ALONG THE WAY, both by measurement rather than argument:
  * CACHE OVERFLOW — setPz silently drops records when full and nothing read the flag. Printed it: 6108/65536, nowhere near full. The ndepth summary now always reports occupancy and overflow.
  * STRIDE / WRONG-WORD — a new near-miss probe (PSXPORT_DEBUG=pznear) probes +/-32 bytes around every miss. Of ~1540 misses per frame, NONE had a recorded depth nearby, so the depths were not in that buffer at all.

DEAD END, and an important one: rolling the generation only on frames that RECORDED something removes the alternation and takes resolved lookups 6.9% -> 23.0% (per-primitive 2.3% -> 8.0%) — and DEPTH-CULLS SPYRO OUT OF THE FRAME. Entries outlive the address they describe, the pool reuses addresses, and stale depths get served as real ones. Half the vertices resolving correctly beats all of them resolving wrongly. Reverted; picture byte-identical to the known-good capture again.

SECOND DEAD END (C146): with that generation change, the interpreter's dynamic mfc2/lw/sw depth tap reports 99.8% of lookups resolved and the frame collapses to near-empty (48 distinct colours vs 1169). It attaches depth to every address a projected value reaches and most are not packet vertices. Now off by default behind PSXPORT_INTERP_DEPTH=1. COVERAGE IS NOT CORRECTNESS — do not quote a coverage percentage as if it were sorted-correctly percentage.

THE SOUND FIX, not yet done: key cache entries by POOL-BUFFER IDENTITY (or invalidate on the pool pointer wrapping) so a reused address cannot alias, instead of extending how long an address-only key stays live. Then the generation can track flips safely and the tap can be made precise enough to earn being on.
status: open
symptom: Native depth: exactly half the frames resolve every vertex and half resolve none (hit=1547/miss=0 alternating with hit=0/miss=1540)
tags: render,depth,widescreen
created: 2026-07-30
updated: 2026-07-30
---

## Root cause


## What was tried / dead ends


## Resolution
