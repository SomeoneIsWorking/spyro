---
id: 77
title: FIELD world recipe reconstructs one near-quad UV assignment incorrectly
status: open
symptom: the semantic world final stream diverges from retail on record 646 of the first FIELD occlusion-group call although all geometry and material identity agree
tags: render,field,world,uv,re
created: 2026-08-22
updated: 2026-08-22
---

## Evidence boundary

`PSXPORT_NATIVE_WORLD=1 PSXPORT_WORLD_SCENE_ORACLE=1 PSXPORT_NDIFF=8` on the 5,000-present
reference path reached stages 13 and 0. Stage-13 flat-list calls continued to pass. On the first FIELD
occlusion-group world call, the native body matched its generated body, and the final semantic stream
matched retail for records 0 through 645. Record 646 was the first difference:

- both streams contained 695 records with the same linked-list validity and family counts;
- source `0x80090ABC`, sector `0x8009091C`, decision `Near`, source ordinal/group identity, four SXY,
  four RGB, all V coordinates, CLUT `0x2420`, TPAGE `0xD088`, textured/semitrans flags, and OT identity
  agreed;
- only U differed: retail `255,224,255,224`, semantic `224,255,255,224`.

The complete evidence is `scratch/logs/gate-boot-20260822-173933.log:5283-5302`. The process abort
and gate exit 139 are the oracle's deliberate fail-fast path, not an ambient crash.

## Exact open boundary

The divergence is confined to the binary-derived texture-word adjustment/vertex assignment for a
near-subdivided textured quad in the FIELD-only occlusion-group corpus. Existing title flat-list
coverage did not exercise this value. Do not wire the FIELD environment submitter until the retail
rule that chooses these four adjusted UV words is derived and the final-stream oracle passes this
corpus. Swapping the two observed U bytes would be a scene-specific patch, not a fix.
