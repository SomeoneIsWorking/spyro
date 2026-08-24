---
id: 77
title: FIELD world recipe omitted the medium-quad texture attribute adjustment
status: investigating
symptom: the semantic world final stream diverges from retail on record 646 of the first FIELD occlusion-group call although all geometry and material identity agree
tags: render,field,world,uv,re
created: 2026-08-22
updated: 2026-08-25
---

## Evidence boundary

`PSXPORT_NATIVE_WORLD=1 PSXPORT_WORLD_SCENE_ORACLE=1 PSXPORT_NDIFF=8` on the 5,000-present
reference path reached stages 13 and 0. Stage-13 flat-list calls continued to pass. On the first FIELD
occlusion-group world call, the native body matched its generated body, and the final semantic stream
matched retail for records 0 through 645. Record 646 was the first difference:

- both streams contained 695 records with the same linked-list validity and family counts;
- source `0x80090ABC`, sector `0x8009091C`, decision `Medium`, source ordinal/group identity, four SXY,
  four RGB, all V coordinates, CLUT `0x2420`, TPAGE `0xD088`, textured/semitrans flags, and OT identity
  agreed;
- only U differed: retail `255,224,255,224`, semantic `224,255,224,255`.

The complete evidence is `scratch/logs/gate-boot-20260822-173933.log:5283-5302`. The process abort
and gate exit 139 are the oracle's deliberate fail-fast path, not an ambient crash.

## Root cause and candidate boundary

The fresh serialized run in `scratch/logs/field-world-oracle-20260825-011156.log` reproduced record
646 from the current tracked decoder and identified the semantic face as `Origin::Medium`; its
texture provenance was zero because only the near path retained that address. This falsified the
earlier near-quad/stale-build interpretation.

Retail `0x80027E98..0x80027ED8` first supplies the medium quad's 31x31 defaults, then uses
`second >> 25` to index `D_8006D058` and replaces all four texture words from that entry. The
semantic medium path supplied only the defaults. For attribute `0x68`, executable entry
`D_8006D0C0` is `0xFFE1001F,0x1F001F1F`, exactly transforming semantic U `e0,ff,e0,ff` into retail
`ff,e0,ff,e0` without a scene-specific swap.

The candidate now shares that executable-derived adjustment rule between medium (31x31 default)
and near (15x15 default) refinement, while retaining each face's authored texture-pair address for
future oracle diagnostics. A production-path Clang regression covers both extents and the exact
attribute-`0x68` words. The issue remains investigating until one later serialized runtime oracle
passes the FIELD corpus; do not submit the FIELD layer before that result.
