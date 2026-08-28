---
id: C230
kind: claim
status: holds
created: 2026-08-28
tags: ownership,memcard,ndiff,frontier
depends: game/core/native_memcard_operations.cpp#startMemcardOperationNative, game/core/memcard_operations.h#memcardOperationPlan, titles/spyro1/core/spyro1_runtime.cpp#Spyro1Runtime::registerOverrides, tests/test_memcard_event_stack.cpp#main
reconfirmed: 2026-08-28 17:10:33
verified_at: 2026-08-28 17:10:33
---

## Claim

Spyro owns the reached libmcrd MemCardExist and MemCardAccept request starters

## Evidence

Current-binary FNTRACE run at scratch/logs/spyro-memcard-parent-trace-20260828.log reached MemCardAccept 0x800665B8 twice, first frame 280 from ra 0x8007B12C, and MemCardExist 0x8006635C 346 times, first frame 287 from ra 0x80032A9C, with zero ABI violations. The native run at scratch/logs/spyro-memcard-operations-ndiff-20260828.log reports MemCardAccept calls 1-2 and MemCardExist calls 1-4 matching their retained generated bodies exactly, alongside the already-owned event-stack child; the focused memcard_event_stack test passes. The native owners retain the generated bodies and implement only the binary-derived idle/busy transaction, operation code, callback address, phase/result reset, request argument, and existing printf/event-stack call boundaries.

## What would falsify it

The current executable stops reaching either starter while its positive title-memory-card controls still run, the operation/callback constants or direct-child set change, the focused contract test fails, or PSXPORT_NDIFF reports any register, RAM, scratchpad, HI/LO, or COP2 divergence for a reached call

## Re-confirmed 2026-08-28 17:10:33

Current-binary FNTRACE run scratch/logs/spyro-memcard-parent-trace-20260828.log reached MemCardAccept 0x800665B8 twice, first at frame 280 from ra 0x8007B12C, and MemCardExist 0x8006635C 346 times, first at frame 287 from ra 0x80032A9C, with zero ABI violations. After simplifying the owner wrappers so the ranker discovers both ndiff sites, the real native run scratch/logs/spyro-memcard-operations-ndiff-20260828-final.log reports MemCardAccept calls 1-2 and MemCardExist calls 1-4 matching their retained generated bodies exactly, alongside the already-owned event-stack child. The focused memcard_event_stack test passes. The native owners retain the generated bodies and implement only the binary-derived idle/busy transaction, operation code, callback address, phase/result reset, request argument, and existing printf/event-stack call boundaries.
