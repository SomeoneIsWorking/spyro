---
id: 93
title: FIELD DrawActors portal aperture path is unowned on the gate route
status: investigating
symptom: gate teleport reaches a visible aperture, but DrawActors' mask and dynamic mesh families are not yet native-owned
tags: render,field,cyclorama,portal,re,ownership
created: 2026-08-28
updated: 2026-08-28
---

## Binary-grounded boundary

Resident `0x80050BD0` (`DrawActors`) advances the two sky-spin words, examines at most six portal
records, builds the aperture/scissor state, runs the separate `0x8004FEA0` fog/mask pass, selects
the near `0x8004F4BC` or fade-band `0x80050240` dynamic mesh family, then invokes owned static-mesh renderer
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

The production-compiled `cyclorama_portal_mesh_recipe` now transcribes both dynamic mesh families'
shared asset walk. It preserves the aperture edges for the near `0x8004F4BC` branch and uses the
near branch's raw vertex colours; the real gate-0 snapshot produces 55 objects / 2,928 authored
candidates, 12 surviving objects, and 94 clipped near triangles. The retained frame still does not
invoke that path. With a deliberately positive full-screen aperture around the snapshot's real
portal-2 asset, the mid/far `0x80050240` path parses 35 objects / 2,897 authored candidates; 10
objects survive, 584 vertices project, 822 candidates reach the face loop, and 553 sources yield
597 clipped triangles. Refusal clears the complete face result, so preparation is atomic. A new
batched native submitter now owns queue admission and publication for one dynamic producer family
at a time: it preserves the guest producer key (`0x8004F4BC` or `0x80050240`), portal OT bin, portal
call order, and per-face Gouraud/depth state. It is independently tested but deliberately not wired
into stage 0, because a visible frame still requires the separate `0x8004FEA0` mask owner first.

## Proper next step

The retained Artisans frame needs no portal draw before its main sky, so it is no longer the blocker
to that frame's cyclorama readiness. A later genuinely visible portal must still ground and own the
`0x8004FEA0` mask, then wire the tested near-family `0x8004F4BC` and fade-family `0x80050240` submitter
with the exact mask/painter/preflight and capacity contracts. The 5-bit actor scissor outcode is distinct from
portal visibility: mesh-level rejection uses `AND(mask)&0x0F`, while triangle clipping uses
`AND(mask)&0x1F`; the edge table is ten `0x18`-byte records plus a zero sentinel at `0x80077EA0`.
Do not infer the mask pass from the synthetic positive aperture, call retained guest renderers, or
suppress a visible mask because its distance-selected mesh is empty.

## Gate teleport experiment

The field's live portal table is inspectable through the title-owned diagnostic REPL command
`gates`. It decodes the portal's `m_PathMoby` at `portal+0x18`, the level target at `portal+0x1c`,
the first aperture point at `portal+0x20`, and the two-node `PathData` reached through the level Moby's
`0x58` byte stride. On a real Artisans run, gate 0 targets level 14, path Moby 40, and its nodes are
`(132301,82883,6799)` and `(136387,82975,6799)`.

`gate-teleport 0 0` writes only the source-backed node position to `g_Spyro.m_Position` and
`m_previousPosition` while the game is in active field state. The next native frame refuses at
`0x80050BD0` because the moved camera exposes a visible portal. The frame has distance `4692`
(`<0x3001`), three aperture edges, and the native near-mesh recipe produces 94 triangles; the
remaining refusal is the unowned `0x8004FEA0` mask/painter boundary. This is evidence that the
coordinate path is valid and that the remaining transition blocker is visible portal rendering; it
is not evidence of a completed level transition. The command remains diagnostic-only and does not
write `g_NextLevelId`, load state, or transition state.
