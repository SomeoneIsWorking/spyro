---
id: 93
title: FIELD cyclorama has five logical portals but no visible portal aperture in the Artisans snapshot
status: investigating
symptom: stage 0 needs an exact empty-portal classification while visible portal mask and mesh families remain unowned
tags: render,field,cyclorama,portal,re,ownership
created: 2026-08-28
updated: 2026-08-28
---

## Binary-grounded boundary

Resident `0x80050BD0` advances the two sky-spin words, examines at most six portal records, renders
every portal whose world-sector is negative or currently broad-visible through the distinct
`0x8004F4BC`, `0x8004FEA0`, or `0x80050240` families, then invokes owned static-mesh renderer
`0x8004EBA8` for the main sky. The final call selects the camera occlusion group only when it is below
`g_Environment.m_OcclusionGroupCount`, otherwise `-1`, with matrices `g_Camera+0x14` and `g_Camera`.

The earlier classification of those five records as five renderer invocations was false. The records
are logically active because every world-sector is `-1`, but the exact portal projection produces no
screen-crossing edge for any record. `scratch/raw/miss_ram.bin` measures:

- portal 0: distance 61,160, shift 1, zero edges, clipped box `512,76..512,88`;
- portal 1: distance 30,988, shift 1, zero edges, clipped box `0,71..0,130`;
- portal 2: distance 16,222, shift 0, zero edges, clipped box `512,75..512,154`;
- portal 3: distance 65,536, shift 1, zero edges, clipped box `512,39..512,78`;
- portal 4: distance 32,508, shift 1, zero edges, clipped box `0,240..0,240`.

The compiled pure scene owner now distinguishes logical activity from an on-screen aperture. It
accepts all five as `ValidEmpty`, derives selection 17 and spin `0x3E2 -> 0x3E4` / `0x80 -> 0x80`,
and remains read-only until the existing main-sky producer succeeds. A separate classification test
proves the opposite answer: an on-screen `ValidEmpty` far-mesh record still refuses because
`0x8004FEA0` owns its mask; a visible mid-distance record refuses as well.

The production-compiled `cyclorama_portal_mesh_recipe` is a reusable, read-only lower-level recipe
for future visible calls to `0x80050240`, not a claim that this retained frame draws a portal. With a
deliberately positive full-screen aperture around the snapshot's real portal-2 asset, it parses 35
objects / 2,897 authored candidates; 10 objects survive, 692 vertices project, 778 candidates reach
the face loop, and 597 sources yield 645 clipped triangles. Refusal clears the complete face result,
so preparation is atomic. No queue submitter, painter order, or stage-0 call is wired.

## Proper next step

The retained Artisans frame needs no portal draw before its main sky, so it is no longer the blocker
to that frame's cyclorama readiness. A later genuinely visible portal must still ground and own the
`0x8004FEA0` mask, near-family `0x8004F4BC`, and the painter/preflight submission contract for the
prepared `0x80050240` faces before the owner can accept it. Do not infer those contracts from the
synthetic positive aperture, call retained guest renderers, or suppress a visible mask because its
distance-selected mesh is empty.
