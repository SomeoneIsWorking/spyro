---
id: 101
title: Spyro gameplay still depends on offline-generated guest bodies
status: resolved
symptom: the product cannot execute the recorded stage-13 and FIELD routes without a generated guest corpus and world-body include
state_items: S008,S009,S010,S011,S012
tags: dynarec,lightrec,static-retirement,world,stage13,product
created: 2026-09-04
updated: 2026-09-04
---

## Root cause

The current product binds title execution to offline-emitted C and generated-symbol dispatch.
`game/core/world_body.inc` further derives an executable guest body into the host build so native
frame ownership can resume around it. Those mechanisms predate the portfolio decision that remaining
guest code must execute on demand from the authenticated image through Lightrec.

This is an execution-ownership defect, not a missing seed or generated-body bug. Regenerating the
corpus, extending the derivative, or falling back to the test interpreter would preserve the wrong
owner.

## Resolution

The static dependency is removed break-first. The generator, corpus, seed manifests, generated-symbol
registration/tests, static CMake path, `world_body.inc`, and its native transcription are absent. The
sole product enters authenticated crt0 through psxport's runtime boundary, and
`WorldGuestExecution` names the unchanged retail `0x800258F0` body through scoped `callOriginal`.

The missing Lightrec backend and its 800/900/gameplay conformance are tracked by S008–S012 rather than
keeping this resolved static architecture alive. No product fallback or gameplay interpreter is
permitted while those capabilities remain incomplete.
