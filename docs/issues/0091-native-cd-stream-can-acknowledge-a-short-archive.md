---
id: 91
title: Native CD stream can acknowledge a short archive copy as successful
status: resolved
symptom: cd_stream_read publishes success and arms completion even when copyArchiveRead moves fewer bytes than requested
tags: spyro1,cd,streaming,contract,latent
created: 2026-08-28
updated: 2026-08-31
---

## Grounded contract defect

`game/core/cd_queue.cpp::copyArchiveRead` returns the number of sequential bytes copied. The shipping
`cd_stream_read` path currently publishes success, arms `cd_completion_pending`, and returns accepted
`v0=1` without comparing that count to the requested length. A short archive read can therefore be
reported as complete.

## Scope

This is not the cause of issue #89 in the measured Artisans transition: the 2026-08-28 PID 3558798
witness requested and copied all 83968 (`0x14800`) scene bytes, with coverage `[0,83968)` and
`complete=1`. Keep the generic contract correction independent from the collision-root corruption
investigation.

## Falsifier / acceptance

Exercise the production decision seam with an intentionally truncated archive and prove that it
refuses completion without publishing a successful transfer. Preserve the complete-read behavior and
the asynchronous queue contract.

### Resolution (2026-08-31)
The shipping cd_stream_read path now derives acceptance, completion arming, and v0 from archive_transfer::decide: a short copy publishes non-pending guest state, clears any host completion latch, returns v0=0, and never identifies the partial buffer as a loaded overlay. The release-build-safe focused test proves a complete 0x14800-byte transfer is accepted and a one-sector-short 0x14000-byte transfer is refused with no completion pending; Clang rebuilt the shipping spyro_port target.
