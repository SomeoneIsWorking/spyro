---
id: 82
title: libmcrd event-stack push 0x80068F44 is reached and matches its retained oracle
status: resolved
symptom: the next boot-to-play ownership frontier has code and focused layout coverage, but its title-screen memory-card path has not yet been re-run under FNTRACE and NDIFF on the current binary
tags: ownership,memcard,ndiff,frontier
created: 2026-08-26
updated: 2026-08-26
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-26)
Root cause and implementation boundary: SCUS_942.28 0x80068F44..0x80068FC0 is libmcrd event-stack push, confirmed by the executable overflow string "libmcrd: event overflow", 13 static callers all in libmcrd, parallel 16-byte state and 4-byte handler tables, and the known-positive title-screen memory-card reach in issue 0027. Its sole direct child is already-owned printf 0x8006279C. `game/core/native_memcard_event_stack.cpp` owns the body through `ndiff_run` while retaining `gen_func_80068F44` as oracle; `test_memcard_event_stack` checks reset/last/overflow indices and both table strides.

The current-binary reference run `scratch/logs/gate-boot-20260826-213811.log` reached 0x80068F44 1,253 times, first at frame 688 from ra=0x80066608, after positive control 0x8005BBF4 reached at frame 0. The native run `scratch/logs/gate-boot-20260826-213915.log` reported calls 1 and 2 of `memcard-event-push@0x80068F44` matching the retained generated body exactly, with no NDIFF divergence. Both main product legs exited 0 and reached stage 13. A separate cold Clang build against exact recorded framework pin `17981527` linked the full product, passed the focused 4/4 CTests, then ran the real product to its 3,000-field cap and exited 0; `scratch/logs/spyro-pin-ndiff-20260826.log` stamps the exact pin and again reports calls 1 and 2 matching. The enclosing shared-head gate remained red because its secondary native-render probe deliberately aborts on the pre-existing unimplemented stage-13 mode 1. That renderer gap is not ownership evidence for or against this body. C224 records the deliberately narrow ownership claim and its falsifier.
