---
id: C210
kind: claim
status: holds
created: 2026-08-21
tags: native,ownership,non-leaf,spu
depends: game/core/native_spu_pio_upload.cpp#writeSpuRamPioNative, game/core/spu_pio_upload.h#spuPioBatchBytes, titles/spyro1/core/spyro1_runtime.cpp#Spyro1Runtime::registerOverrides, tests/test_spu_pio_upload.cpp#main
reconfirmed: 2026-08-22 18:45:06
verified_at: 2026-08-22 18:45:06
---

## Claim

Spyro natively owns the reached PsyQ WriteSpuRamPio body at 0x8005BE88 while retaining its generated oracle and already-owned spin/printf children.

## Evidence

SCUS_942.28 disassembly, Ghidra scratch/decomp/psyq_8005be88.c, and both byte-matching Spyro references identify the 115-instruction PIO upload. The corrected dependency-ready ranker proves its only direct children are owned 0x8005C720 and 0x8006279C. scratch/logs/gate-boot-20260821-032040.log reaches it once at frame zero from ra=0x8005BD94 with a0=0x80073594/a1=16; scratch/logs/gate-boot-20260821-032535.log reports spu-pio@0x8005BE88 call #1 matches the retained generated body exactly. Focused spu_pio_upload CTest covers zero, odd, full 0x40-byte, and multi-batch boundaries.

## What would falsify it

The executable or generated body changes, the frame-zero override no longer fires, the batching seam fails its focused boundary test, a direct child stops being independently owned, or PSXPORT_NDIFF reports divergence.

## Re-confirmed 2026-08-21 03:30:41

Final exact-tree Clang evidence: focused spu_pio_upload CTest passed 1/1 and full CTest passed 10/10 including cpp-policy 39/39; scratch/logs/gate-boot-20260821-032916.log line 64 reports spu-pio@0x8005BE88 call #1 matches the retained recompiled body exactly; ordinary shipping gate scratch/logs/gate-boot-20260821-032943.log passed 14/14 at 3,000 fields, 13 scenes, and 704625 native-producer primitives against psxport 2b5ef7b5522f3b879b69315acd11a037ca7a78bb.

## Re-confirmed 2026-08-21

Post-landing current-tree NDIFF log gate-boot-20260821-032916.log reports spu-pio@0x8005BE88 call #1 exact; focused spu_pio_upload CTest passed.

## Re-confirmed 2026-08-21

Post-landing long NDIFF kept WriteSpuRamPio child calls exact and full CTest passed 13/13.

## Re-confirmed 2026-08-22 18:45:06

Registration moved into Spyro1Runtime::registerOverrides. The rebuilt SCUS_942.28 shipping gate gate-boot-20260822-184226.log reported spu-pio@0x8005BE88 call #1 matching the retained body; the focused test and full 27/27 CTests pass.
