---
id: 105
title: Actor depth disagrees with retail's OT bin for some instance pairs, and the geometry says retail is right
status: open
symptom: about 10% of ordered actor primitive pairs sort in the opposite order from retail, and for the worst pair the port places the FARTHER moby in front
tags: render,field,actor,depth,oracle
created: 2026-09-10
updated: 2026-09-19
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

## 2026-09-19: a USER saw this, and a third hypothesis the list above does not have

A user reported "Spyro looks like gems that should be behind terrain rendering on top" (issue 0120).
Chasing that symptom produced a new measurement of THIS issue: the same instrument, the same shape
of disagreement, and this time the worst pair is a GEM.

Do not treat 0120 as a duplicate of this issue. `actor_oracle_diff.py` walks retail's moby list and
cannot see terrain, so it can say nothing about an actor sorting against terrain -- which is what
0120 reproduces at the framebuffer. The two may share a cause; nothing measured so far says they do.

`tools/actor_oracle_diff.py` over `replays/gameplay/artisans-arrival.pad`, last of 220 oracle frames:

```
comparable ordered pairs : 174417
disagreeing with retail  : 8850   (5.07%)
bins 171 vs 105 (span 66) but native depth 0.012609 vs 0.009528
     (8016F0C8 class 83 scale 0 dist 26333 / 8016FA10 class 10 scale 0 dist 17518)
```

Same signature as the recorded class-339/class-354 pair: both `scale 0`, the FARTHER instance
(26333 vs 17518) given the LARGER `ord`, and `pzToOrd` being reversed-Z means larger is nearer. The
geometry again agrees with retail.

The producer table for that frame rules out an attribution problem, which was hypothesis 1 in "Where
to look next": every producer that draws these matches retail's own walker on geometry with zero
unmatched — `0x8001F798` 357/357, `0x80020F34` 75/75, `0x80022A2C` 41/39 (2 recoloured by one
colour count), `0x80023AC4` 173/173. The instances are correctly attributed; only their depths sort
wrongly.

### The new hypothesis: the recipe computes retail's order and the submitter discards it

`game/render/field_shaded_queue_recipe.cpp` derives the OT bin the way retail does, and it is NOT
just the vertex depths:

```cpp
int64_t depth = projected[i0].sz + projected[i1].sz + projected[i2].sz + projected[i3].sz;
depth -= (int64_t)std::max(record.affine.t[2] - 256, 0) * 4;   // actor-origin bias
if (reverseFacing) { depth += 512; }
const int64_t ot = depth >> 5;
```

`game/render/field_shaded_queue_submitter.cpp` then submits `depth[i] = pzToOrd(vertices[i].viewZ)`
— raw per-vertex view-Z, with **neither the actor-origin bias nor the reverse-facing term**. The
bias is proportional to `affine.t[2]`, the instance's own view-Z origin, so its omission grows with
distance: exactly the observed signature of the FARTHER instance being placed nearer.

This is distinct from hypothesis 2 above (a depth-unit mismatch between `worldAffine` and
`affineFrom`). Here the two quantities are not even meant to be the same number: one is retail's
ordering key including a per-instance bias, the other is a geometric depth. Check the other actor
producers for the same shape before treating it as specific to the shaded queue.

Do not fix this by tuning a constant until the pair agrees. Either the recovered bias is carried
into the submitted depth in its correct scale, or the face carries its authored order through
`RqItem::authored_depth` / `sort_key` — and that route first needs the painter-object path and the
keyed-face path to compose (the frame's census shows 0 keyed faces of 1747 because every prim
carries a painter object). The falsifier either way is this named pair plus the 5.07% rate.

## RETRACTED 2026-09-19, same day it was added: the "submitter discards the bias" hypothesis is wrong

The hypothesis added above -- that `field_shaded_queue_recipe.cpp` computes retail's OT bin with the
actor-origin bias and `field_shaded_queue_submitter.cpp` then throws it away -- is FALSE. Reading
the whole submitter instead of its depth line shows the bin is passed on every single face:

```cpp
// field_shaded_queue_submitter.cpp, the last argument of every emitOrQueue call
scene_painter_order::queuedWorld(face.otBin, face.paintGroup)
```

and `prepare()` in the same file REFUSES the whole plan if any face's order is not authored:

```cpp
if (... || !scene_painter_order::queuedWorld(face.otBin, face.paintGroup).authored()) {
  plan.status = Status::InvalidOrder;
```

`face.otBin` is the recipe's `ot`, bias and reverse-facing term included. So the producer does not
compute an answer and discard it. It submits geometric per-vertex depth for occlusion against
terrain AND retail's authored position for ordering, which is a coherent design rather than a bug.
`scene_painter_order.h` shows every actor producer doing the same -- `actor`, `secondaryActor`,
`pairedActor`, `spyroShadow`, `mobyShadow`, `flame`, `glow`, `sparkle`, `particle` and both
cyclorama entries all take an `otBin`.

### How the mistake happened, because the instrument will mislead the next reader too

The pixel probe prints `authored={}` from `RqItem::authored_depth`, and 15,592 of 15,592 probed
lines read `authored=0`. That was read as "no face carries an authored order". It does not mean
that. `authored_depth` is documented in `render_queue.h` as "1 = depth[] already encodes OT order;
suppress the generic later-draw bias" -- a statement about the depth ARRAY, not about painter order.
The field that carries retail's bin is `RqItem::painter_replay`, and the probe did not print it at
all. A probe that omits the only field bearing on the question, while printing a similarly-named one
that does not, cannot show the other answer.

Fixed in psxport: the probe now prints `replay_domain`, `replay_ot`, `replay_link` and `replay_sub`,
and the old `authored=` is renamed `authored_depth=` so the two cannot be confused again.

### What is still open, unchanged

The measured disagreement is real and this retraction does not touch it: 8,850 of 174,417 comparable
ordered pairs (5.07%) sort against retail, and the worst pair puts the farther gem in front. What is
now unknown again is WHY. The next measurement is the one the probe was just taught to make -- at a
pixel where the native and retail disagree, read `replay_ot` for both primitives and compare it with
retail's bin. If the replay positions are right, the ordering rule or the depth buffer is overriding
them; if they are wrong, the recipe's bin is wrong. Those are different bugs and the probe can now
tell them apart.

Hypotheses 1 and 2 in "Where to look next" above are untouched by this retraction.

## 2026-09-19, MEASURED: the recipe's own bin is wrong, and it is wrong on the gem

With `RqItem::painter_replay` now logged by the actor oracle, the diff can compare the port's OWN
authored bin against retail's, instead of comparing retail's bin against the port's submitted depth.
That is the comparison this issue needed and never had. Same run, same route
(`artisans-arrival.pad`), 220 oracle frames, reporting the last:

```
depth agreement over matched primitives:
  comparable ordered pairs : 174417
  disagreeing with retail  : 8850   (5.07%)

authored bin vs retail bin (independent of depth):
  compared 664 of 664 matched primitive(s) that carry one
  identical to retail : 625  (94.13%)
  differing           : 39   most common deltas (native - retail):
    -62: 3   -143: 2   -68: 2   -69: 2   -46: 2   -130: 1
    worst 8016F0C8 painter=0x80022A2C native bin 17 vs retail 171 (-154)
    worst 8016F0C8 painter=0x80022A2C native bin 18 vs retail 171 (-153)
    worst 8016F0C8 painter=0x80022A2C native bin 19 vs retail 171 (-152)
    worst 8016F0C8 painter=0x80022A2C native bin 22 vs retail 171 (-149)
    worst 8016F0C8 painter=0x80022A2C native bin 23 vs retail 171 (-148)
```

**Every producer authors a position** — 664 of 664, so the retraction above is confirmed from the
other direction as well. **94.13% of authored bins are byte-identical to retail's**, which is the
denominator that makes the remaining 39 worth reading: the recipe is right nearly everywhere, and
wrong in a concentrated place.

That place is `8016F0C8`, painter `0x80022A2C` — the shaded-moby/gem queue — and it is the SAME
instance as this issue's worst ordered pair (`8016F0C8 class 83 scale 0 dist 26333`). The native
authors bins 17-23 where retail authors 171. The recipe is not being overridden downstream; it is
computing the wrong answer by roughly 150 bins.

### The arithmetic points at the actor-origin bias

`ot = depth >> 5`, so retail's bin 171 corresponds to a depth near 5,472 and the native's 17 to a
depth near 544 — a shortfall of about 4,900. The recipe's only subtractive term is

```cpp
depth -= (int64_t)std::max(record.affine.t[2] - 256, 0) * 4;   // actor-origin bias
```

which reaches 4,900 at `t[2]` near 1,480. For a distant instance that term can consume most of the
summed vertex depth and crush the bin toward the front of the OT, which is exactly the observed
direction and exactly why the symptom is a FAR gem appearing in front.

So the original suspicion about this bias was directionally right and wrong about where: it is not
that the submitter discards the term, it is that the recipe applies it and the result does not match
retail for these records. Whether retail applies it at all for this record type, applies it with a
different shift, or reaches this face by a path that skips it, is the open question.

### Do not fix by deleting the term

It was recovered from the authenticated renderer and issue 0111 settled the surrounding depth unit.
Removing it would move this instance and would very likely break the 625 that currently agree. The
discriminator is cheap now: change nothing, and first measure whether the 39 differing records share
a `t[2]` range, a class, or a `clipMode`/`nearCamera` branch that the 625 agreeing ones do not.

### The falsifier for any fix

`identical to retail` must rise from 625/664 and the `8016F0C8` bin must move from 17-23 toward 171,
with the 5.07% pair-order disagreement falling. A change that moves the pair rate without raising
the bin agreement has not fixed this and should be rejected.

Run it with:

    tools/actor_oracle_diff.py <log> --frame -1
    tools/actor_oracle_diff.py --selftest x    # proves the report shows BOTH answers
