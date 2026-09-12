---
id: 109
title: World temporal admission refused visible endpoints with pending animation channels
status: resolved
symptom: World interpolation rejected otherwise compatible consecutive frames when a sector's previous endpoint still carried a live animation stamp
tags: render,temporal,animation,world
created: 2026-09-12
updated: 2026-09-12
---

## Cause

`world_scene::sample` correctly refuses a source whose packed arrays have not been advanced, but
the temporal endpoint retained before the frame producer ran could legitimately carry that pending
stamp. The admission walk had no way to materialize the endpoint, so it returned `ActiveAnimation`
for three visible Artisans sectors even though the current frame had already advanced them.

## Fix and evidence

`world_temporal::History::materializePending` now reuses the midpoint culling walk to identify only
visible pending channels. It decodes each channel with `world_animation::appendSector`, validates
the destination shape against the retained LQ/HQ arrays, retires the copied endpoint stamp, and
records the animation metadata/payload spans in both endpoint residency sets. The retained source
is patched atomically; guest RAM is never written. Hidden channels keep the existing refusal path.

The focused animation test passes 62 checks and the temporal test passes 135 checks. A fresh
`tools/drive.py gameplay --after 300 --skip-transitions` route reached GS_Playing and exited 0 with
2,386 admitted world intervals, zero `active_animation` or endpoint-materialization refusals, and
zero JIT fallback blocks. The full independent-console packet comparison remains open.
