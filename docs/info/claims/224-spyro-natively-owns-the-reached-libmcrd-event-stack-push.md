---
id: C224
kind: claim
status: holds
created: 2026-08-26
tags: ownership,ndiff,reach,memcard
depends: game/core/native_memcard_event_stack.cpp#pushMemcardEventNative, game/core/memcard_event_stack.h#memcardEventPushPlan, titles/spyro1/core/spyro1_runtime.cpp#Spyro1Runtime::registerOverrides, tests/test_memcard_event_stack.cpp#main
---

## Claim

Spyro natively owns the reached libmcrd event-stack push at 0x80068F44 while retaining its generated body as the per-call oracle

## Evidence

The SCUS_942.28 slice 0x80068F44..0x80068FC4 is 128 bytes / 32 instructions with SHA-256 `7b25bd5394d6eb44fbe836882eb7bb866ec7c917813ebdd6e17b7037cc9e06ce`. The executable body, byte-matching `external/spyro-1/asm/psyq.s`, and `generated/shard_6.c` agree on the signed four-entry bound, the 16-byte state-record stride, the parallel 4-byte handler table, the four-word clear, and the already-owned printf child. The focused `memcard_event_stack` test exercises the reset, signed-negative, last-valid, and overflow answers plus both strides. A cold Clang build against exact recorded framework pin `17981527` linked the full product and passed the focused CTests 4/4. Current-binary reference log `scratch/logs/gate-boot-20260826-213811.log` reached 0x80068F44 1,253 times, first at frame 688 from ra=0x80066608, while positive control 0x8005BBF4 reached at frame 0. Native log `scratch/logs/gate-boot-20260826-213915.log` reports calls 1 and 2 of `memcard-event-push@0x80068F44` matching retained `gen_func_80068F44` exactly, with no NDIFF divergence. The main product leg exited 0 and reached stage 13 in both runs. The independently rebuilt exact-pin product then ran to its 3,000-field cap and exited 0; `scratch/logs/spyro-pin-ndiff-20260826.log` stamps framework `17981527` and again reports calls 1 and 2 matching exactly with no divergence. The gate's separate native-render probe still aborts on the pre-existing unimplemented stage-13 mode 1; that result is not included in this ownership claim.

## What would falsify it

The executable slice or direct-child set changes, the focused signed-bound/table-layout test fails, the current title-screen memory-card path no longer reaches 0x80068F44 while its positive control still reaches, or PSXPORT_NDIFF reports a RAM, scratchpad, register, HI/LO, or COP2 divergence for a reached call
