---
id: 150
title: Two native overrides differ from retail on the no-input attract route, on main as well as on the branch that moved the field owner
status: open
symptom: `tools/reach_corpus.py`'s attract-demo route reports one mismatching sampled call each in `camera_collision_update` (0x80034480, call 3328) and `allocate_particle_slot` (0x80053570, call 39680), identically at `7d7f2eb` and at `a915b23`
tags: overrides,differential,attract-demo,camera,particles
created: 2026-09-30
updated: 2026-09-30
---

## The measurement

Both builds, the no-input attract route, 420 s, the corpus's own instruments armed (reach recorder
and the 62-selector override differential, first 16 calls of each and every 64th after):

| build | `camera_collision_update` | `allocate_particle_slot` | other selectors |
|---|---|---|---|
| `7d7f2eb` (`main`) | 115 sampled, **1 mismatch** | 637 sampled, **1 mismatch** | 0 |
| `a915b23` (this branch) | 115 sampled, **1 mismatch** | 637 sampled, **1 mismatch** | 0 |

The mismatching calls are the same calls, with the same verdict and the same first difference:

```
camera_collision_update @0x80034480 call 3328 (sample 68): verdict match -> mismatch
  first difference ram [0x00077E04,0x00077E06): original 0C46 native 1473
allocate_particle_slot @0x80053570 call 39680 (sample 636): verdict match -> mismatch
  first difference register ra: original 0x800535A0 native 0x8008A4BC
```

The very next sampled call of each override matches again (`mismatch -> match`), which is the shape
of a single divergent call rather than a drifted state.

## What this is NOT

It is **not** a regression from moving the field delivery into `game/core/field_owner.*`. That was
the working hypothesis, and it is refuted: the two builds' guest RAM is byte-identical at present
10,000, their field traces are identical over 35,734 delivered fields, and their logs are identical
line for line apart from two log strings. See
[0149](0149-shared-field-owner-measured-against-spyro-1-s-own.md) for the full measurement.

## Why it was nearly read as one

The attract route ends when its 420-second clock expires, and how far into the game a run gets in
420 seconds depends on how fast the host is that day. A slower host samples fewer calls. A run that
ends early — as the recorded `main` run did, aborting at field 14,832 with a `stage selector = 5`
cyclorama refusal — never reaches call 3328 or call 39,680 at all, and reports zero. That is how a
mismatch count came to look like a behaviour difference between two builds when it is a depth
measurement.

## What is left

* `allocate_particle_slot`: the first difference is a REGISTER, `ra`, with zero memory differences.
  `ra` is the return address the caller will use, so the native body returned through a different
  path than retail's. The guest address `0x8008A4BC` is not the retail return `0x800535A0`, so
  something in the native body tail-jumps or falls through where retail returns. One call in 39,680.
* `camera_collision_update`: a two-byte difference at guest RAM offset `0x77E04` (`0x80077E04`),
  `0x0C46` against `0x1473`. That is a value, not a pointer, so it is a computed result differing
  from retail's.

Neither is diagnosed here. Both are sampled-call findings on one route, and the driver is a
420-second no-input run, so the unblocking step is the same for each: shadow EVERY call of that one
override on this route (`PSXPORT_OVERRIDE_DIFF_EVERY=1`, one selector at a time) and read the first
divergent call, the way
[0148](0148-the-attract-demo-crashes-in-the-native-player-producer.md) did for
`advance_body_animation_with_transitions`.
