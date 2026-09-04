---
id: 101
title: Spyro gameplay still depends on offline-generated guest bodies
status: open
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

## Required resolution

Integrate the shared per-`Core` Lightrec executor, replace generated-symbol registration with complete
image-generation plus address identity, and route scoped original calls through the runtime. Reproduce
the 800-field boot/title and 900-field mode-2 stage-13 routes with nonzero Lightrec blocks and the
native frame/field owners active. Replace `world_body.inc` with bounded suspension and resumption of
the unchanged retail world body.

The 800/900 runs are first discriminators only. Keep the old path until a representative interactive
Spyro 1 route also proves WAD invalidation in both directions, native and scoped-original dispatch,
independent-oracle state, no linked/selectable interpreter, and host correctness/performance. Then
delete the generator, corpora, emission-only seeds, generated dispatch/tests, world-body include, and
offline provisioning/build path in one landing.
