---
id: 105
title: Actor depth disagrees with retail's OT bin for some instance pairs, and the geometry says retail is right
status: open
symptom: about 10% of ordered actor primitive pairs sort in the opposite order from retail, and for the worst pair the port places the FARTHER moby in front
tags: render,field,actor,depth,oracle
created: 2026-09-10
updated: 2026-09-10
---

## Symptom

`tools/actor_oracle_diff.py` over a seeked Artisans capture reports a steady 9–15% depth
disagreement rate across every frame of the walk, with the same worst pair recurring:

```
depth agreement over matched primitives:
  comparable ordered pairs : 53832
  measured orientation     : a larger OT bin means a smaller native depth
  disagreeing with retail  : 5348
  disagreement rate        : 9.93%
  bins 87 vs 47 (span 40) but native depth 0.026381 vs 0.022987
      (8016D650 class 339 scale 0 dist 11706 / 8016F6F8 class 354 scale 0 dist 7245)
```

Reproduce:

```
python3 tools/drive.py gameplay --seek-class 83 \
  --env PSXPORT_ACTOR_SCENE_ORACLE=1 --env PSXPORT_ACTOR_SCENE_ORACLE_CLASS=355 \
  --debug actororacle --log scratch/logs/gem-depth.log
python3 tools/actor_oracle_diff.py scratch/logs/gem-depth.log
```

## Why this is a port fault and not a granularity artefact

Retail sorts a whole moby into one ordering-table bin; the port gives every vertex its own depth, so
the two answers are allowed to differ where two models genuinely interleave. That is not what this
is. The oracle now prints each drawn instance's world position and the camera's, so the geometry is
available as a third opinion:

* class 339 is **11706** units from the camera, class 354 is **7245**.
* Retail puts 339 in bin 87 and 354 in bin 47 — farther is a larger bin, so retail agrees with the
  geometry.
* The port gives 339 the **larger** `ord`, and `ProjParams::pzToOrd` is a reversed-Z mapping, so a
  larger `ord` is NEARER. The port has them inverted.

Both instances' depths cluster tightly within themselves (339: 0.026381/0.026545/0.026568/0.026765),
so this is a whole-model offset, not per-face relief.

## Hypotheses tried and refused

**The `+0x57` Moby scale byte.** `scaledTranslation` multiplies the view translation, which moves an
instance's depth as well as its size, and it was added recently — the obvious suspect. The oracle's
instance line now prints the byte: **both instances read `scale 0`**, so `scaledTranslation` returns
the translation untouched and cannot be the cause.

**The model descriptor's byte 5 (`transformShift`).** `0x8001F84C` shifts the view translation by it
per record, and retail's own ordering key CR14 is deliberately computed from the raw depth BEFORE
that ladder (`actor_prefix_builder.cpp`, `cr14`), so a native depth buffer comparing two records in
their own fixed-point exponents looked like the defect. Implemented as
`pz * 2^(transformShift - 2)`, threaded from the builder through `PrimitiveInput` to
`actor_face_submitter`. **Measured: the disagreement rate rose from 9.93% to 14.63%.** Refused and
reverted. The conclusion to carry forward is that `native_projection::project` already returns a `pz`
in a unit that is common across records, so the record exponent must NOT be reapplied on top of it.

## Where to look next

Not yet the projection unit and not the scale byte. The next discriminators, in order:

1. Whether `face.moby` (and therefore `dbg_node`) is correctly attributed for these two instances —
   if it is not, the report's per-node identity is wrong and the pair is misread.
2. Whether the disagreeing pairs correlate with a producer boundary: the frame's items split across
   `0x8001F798` (205), `0x80023AC4` (186), `0x80059A48` (16) and `0x80059F8C` (4), and
   `actor_transform_math::worldAffine` doubles the view translation with no per-record shift while
   `actor_prefix_builder::affineFrom` shifts it, so the two paths need not share a depth unit.
3. Whether retail's bin for these instances comes from a path other than CR14.
