# 0106 — Walking up to a portal aborts: the near-portal family emits no faces

Status: open
Affects: `docs/project-state.md` — homeworld portal presentation; blocks reaching a level on foot,
and with it live observation of the stage-10 return-home cancellation (`0104` is unrelated).

## What happens

Driving Spyro toward an Artisans portal (`tools/drive.py gameplay --seek-portal`) reaches distance
`0x13CC` and the frame aborts:

    NATIVE RENDER NOT IMPLEMENTED — stage selector = 0
    (cyclorama producer 0x80050BD0 refused its atomic recipe)

## The measurement

`prepareFrame` classifies a portal at `distance <= 0x3000` as `NearFamilyUnsupported`, which routes
its recipe to the near producer `0x8004F4BC` instead of the mid/far `0x80050240`. The recipe is
built from the SAME contracted aperture half-planes as the mid/far path, on the assumption recorded
in `cyclorama_portal_mesh_recipe.cpp` that the near renderer consumes them unchanged. Measured, with
the `fieldsky` per-draw report added in this change:

    near draw 0: portal=2 frame=near family unsupported/near_portal_family recipe=valid empty/none
      faces=0 distance=5068 mask=true objects=10/35 candidates=893 box_rejected=155
      aperture_rejected=738 accepted=0

So the recipe DID look: 10 of 35 objects survived the depth cull and 893 candidate triangles were
examined. Every one was then discarded — 738 of them by the aperture half-planes specifically, and
none survived.

That does NOT generalise to "the near family never works": `docs/project-state.md` records a gate-0
teleport whose near-family recipe produced 94 clipped triangles through the same aperture. So the
aperture is not always empty, and the open question is what distinguishes the two camera poses —
the walked approach at `0x13CC` versus the teleport — rather than whether the near path runs at all.

The abort itself is a second, separable fact: `build` reports `ValidEmpty` for an
all-clipped recipe (`refusal = "none"`, a legitimate outcome), but
`cyclorama_portal_submitter::prepare` only skips a draw when the frame AND the recipe are both
`ValidEmpty`. A live frame whose recipe emits zero faces is read as `InvalidRecipe`. Do not "fix"
this by accepting the empty recipe: an empty near portal in the player's face is wrong output, and
accepting it would replace a loud abort with a silently missing portal.

## What retail does, read from the routine

`func_8004F4BC` references `D_80077EA0` — the aperture half-plane class — and reaches it through
`func_8004F7E8`, which is the clipper: it loads the record's first word, treats zero as the end of
the list (`beqz` to the unclipped emit at `.L8004FC3C`), loads the segment's two packed `SXY`
endpoints, and runs `NCLIP` against each of the triangle's three vertices to build a three-bit side
mask that indexes the `D_8004F890` jump table; the recursion advances `$t2` by `0x18` per record.

So the near family clips against the SAME half-plane list as the mid/far path, and an EMPTY list
means "emit whole", not "emit nothing". The assumption recorded in `cyclorama_portal_mesh_recipe.cpp`
is therefore right about the clip SOURCE, and the fault is in the list this frame carries or in how
the port applies it — not in the choice of clipper.

Measured for the aborting frame: `edges=1 clip=(0,0)-(684,240)`. One half-plane, and the aperture's
bounding box is the whole frame, which is what a portal filling the view looks like. A single
half-plane that rejects 738 of 893 candidates is what an INVERTED half-plane looks like: it keeps the
outside and discards the inside.

## The orientation discriminator was run, and it is inconclusive here

`edgesKeepingCentre` reports how many retained half-planes keep the aperture's own projected
centroid; an inverted half-plane keeps the complement, so 0 of 1 would normally convict it. Measured:

    edges=1 keeping_centre=0 centre=(-283,-205) clip=(0,0)-(684,240)

The centroid projects to `(-283,-205)`, off the screen entirely, while the aperture's bounding box
covers the whole frame. At this distance the camera is close enough that aperture points straddle the
projection's near boundary, so the projected centroid is no longer an interior point and the test
cannot separate an inverted half-plane from a degenerate projection. Recorded as a negative rather
than deleted: the coordinates are what showed the degeneracy.

Structure confirmed from `func_80050BD0.s` meanwhile: retail writes `1` into each accepted record's
first word at `ordinal * 0x18` (`0x8005143C`, `0x80051BD8`) and a terminating `0` at the count's
own `0x18` stride (`0x80051854`, `0x80051EE0`). So the port's edge vector has the right shape and the
disagreement, if any, is in WHICH segments retail accepts — its filter at `0x800513F4` also collapses
a segment whose `|dx|` or `|dy|` is under 3, which the port's `crossesScreen` transcription does not.

## Retail builds the aperture TWICE per portal, and the port builds it once

Read from `func_80050BD0.s`, per portal, in order:

1. an edge loop writing `D_80077EA0` (flag store `0x8005143C`, terminator `0x80051854`), then
   `jal func_8004FEA0` at `0x800518C4` — the MASK;
2. a second, different edge loop writing the same `D_80077EA0` (flag store `0x80051BD8`,
   terminator `0x80051EE0`), then `jal func_80050240` at `0x80051F38` and `jal func_8004F4BC` at
   `0x80051F54` — the far and near MESHES.

The two loops are not the same code. Loop 1 accumulates the point sums into stack slots `0x128`
and `0x130` and publishes `D_80075934`; loop 2 reads `D_80075934` instead and indexes its records
from `D_80077EAC`. Loop 1 collapses a segment whose `|dx|` or `|dy|` is under 3 — two
`slti $v0, $v0, 0x3` sites at `0x80051410` and `0x8005142C`; loop 2 contains no such test at all
(measured: 2 occurrences in loop 1's range, 0 in loop 2's).

The port has ONE `frame.edges`, built by `prepareFrame`, and `cyclorama_mask_recipe` and
`cyclorama_portal_mesh::build` both clip against it. The centroid accumulation and the sub-3
collapse in that transcription come from loop 1, so what the port feeds the near mesh is the MASK's
aperture, not the mesh aperture retail hands `0x8004F4BC`. That is the leading explanation for one
retained half-plane discarding 738 of 893 candidates, and it is why the mask keeps rendering
correctly on the same frame the meshes go empty.

## Next discriminator

Compare the two poses directly: dump `frame.edges`, the clip rectangle, and the projected aperture
points for the teleport frame that emitted 94 triangles and for this walked frame that emitted 0.
If the walked frame's half-planes enclose no screen area, the aperture construction is the fault; if
they enclose area the geometry misses, the near family's clip source is, and what `0x8004F4BC`
actually clips against must be recovered from the retail routine rather than from the mid/far
transcription. `0x80050BD0` selects between the two families at the same `0x3000` threshold, so the
selection itself is already recovered.
