---
id: 0171
title: The native terrain drawer mismatches retail on 633 of 634 sampled calls on the title_route
  route, first divergence at GTE data register 6
status: open
symptom: tools/title_route.py --title spyro2 with PSXPORT_OVERRIDE_DIFF=0x80023BB4 and
  PSXPORT_OVERRIDE_DIFF_EVERY=1 reports 633 of 634 sampled calls mismatched, the first at call 1 on
  gte data register 6 (VXY1). The same route on Spyro 3 (0x80022378) reports 690 of 691, also first
  at call 1 on register 6. The override differential is the gate this port uses to claim a native
  drawer matches the guest body it replaces, and on this route it does not match at all.
tags: render,terrain,differential,gt,shipping-defect
created: 2026-10-02
updated: 2026-10-02
---

## The measurement, and what it is not

Run twice on 2026-10-02 at 4:3 (`PSXPORT_SETTINGS=tools/shipping_settings.ini`, `aspect=1`), once
against HEAD and once against the terrain-sink refactor:

```
title    override      calls  match  mismatch  incomparable
spyro2   0x80023BB4      634     0     633      1  (async pending work, one call)
spyro3   0x80022378      691     0     690      1  (async pending work, one call)
```

Both differential reports were captured to `scratch/s3wide/diff_s2_{head,refactor}.json` and
`scratch/s3wide/diff_s3_{head,refactor}.json`; the two revisions are byte-identical to each other,
which is what proves the refactor changed nothing, and is NOT evidence about retail.

## Why this does not contradict the recorded 25/25 and 26/26

`docs/project-state.md` records Spyro 2's terrain drawer at 25/25 and Spyro 3's at 26/26, and both
of those were sampled, not exhaustive: the default `PSXPORT_OVERRIDE_DIFF_EVERY` keeps the first 16
calls of each override and every 64th after. This issue's runs set `_EVERY=1`, so every call is
shadowed, and `docs/issues/0148` already recorded that a sampled gate passed 17/17 and then
mismatched 8 of 395 once every call was shadowed. The recorded rows are therefore not false, and
they are also not strong enough to say the drawer matches retail over a whole route.

**The rows should be read as what they are: a sampled pass on a short route.** Nothing in this issue
disturbs them; what it removes is the assumption that they speak for the route.

## Why it matters beyond the differential

The terrain drawer is the single largest native override in this port -- 5,487 guest instructions,
measured identical between the two images -- and it is what draws the level in both titles. A drawer
that agrees with retail on 16 sampled calls and disagrees on 633 others is either diverging early
and staying diverged, or diverging on a branch the short route never reaches. Both are shipping
defects in the picture the player sees, and the moby walk's own numbers on the same runs (Spyro 2
153/634, Spyro 3 231/691) say the same route is not a route on which these overrides have been
verified end to end.

## Where to start

The first divergence is at the very first sampled call and on GTE data register 6, which the drawer
publishes itself:

`game/render/guest_terrain_drawer.cpp`, `Drawer::run` --
`gte_write_data(gte::kVxy1, frame.scratch)` with
`frame.scratch = core_.mem_r32(globals_.scratchBaseWord) - facts_.scratchListsBelowEnd`.

That is the first thing the native path does the guest body would also do, so either the derivation
of `scratch` is wrong, or the register the guest leaves in VXY1 on entry is not what the derivation
assumes. Ghidra's answer for the drawer's own prologue is where to look first:

```
external/psxport/tools/decomp_pipeline.py --image scratch/assets/spyro2/SCUS_944.25 \
    --function-at 0x80023BB4
```

and the classification pass that immediately consumes it
(`game/render/guest_terrain_classify.cpp`, `SectorClassifier::classify`, which writes one visibility
byte per sector at `kScratchpad` and reads the camera back out of the same region) is the second
place to look. Do not widen the gate or loosen the comparison to make this pass: the whole value of
the differential is that it is strict.

Related: the sink seam this measurement was taken during is in the same change and is byte-identical
to HEAD, so nothing in that seam can be the cause.