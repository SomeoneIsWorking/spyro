# 0106 — Walking up to a portal aborts: the near-portal family emits no faces

Status: open — the facing test now blocks the portal route; mesh visibility fixed; the mask still clips against the mesh aperture
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

## Cause, found

Dumping the aperture's projected points for the aborting frame settled it:

    near draw 0 points (5): (-1022,1021,-4508) (-1022,-1022,-5124) (-908,-1022,-5055)
      (623,-1022,-4824) (913,1021,-4134)

Every point saturates at the GTE's screen clamp and every view Z is NEGATIVE: walking up to a portal
puts its aperture behind the camera. Retail handles that explicitly. At `0x80051D0C`-`0x80051E70`
it computes a visibility flag from the post-contraction extents, the summed view Z in `D_80075934`
and the retained edge count, and `beqz $s1, .L80051F5C` skips BOTH mesh calls when it is zero; the
same gate at `0x800517DC` skips the mask. For this frame the summed Z is negative, so retail draws
nothing for the portal.

The port had no such gate. It built an aperture out of the saturated points, handed it to the mesh
recipe, which clipped all 893 candidates away and returned an empty recipe, which the submitter then
read as an invalid one and aborted the frame on.

`meshVisibility` now carries retail's decision and `prepareFrame` returns `ValidEmpty` when it says
hidden. Measured after the fix: the same portal walk runs 1,684 frames with zero refusals and no
native-render abort. Two unit tests cover it, one built from the measured points above.

The `s1 == 2` sub-case — the aperture qualifies but no point is on screen, and the portal is closer
than `0x1000` — needs the camera-facing dot product retail builds through `RotVec8ToMatrix`. That is
now recovered too, below, so the port no longer has an unrecovered cyclorama path.

## The route reaches the portal, and now crosses it

`game/core/spyro_gate_debug.cpp` had a `gates` / `gate-teleport` REPL pair that nothing could reach:
no runtime overrode `GameRuntime::replCommand`, so the REPL answered `? gate-teleport`. Wiring it
through `Spyro1Runtime::replCommand` makes it work, and it lists Artisans' five gates with their
target levels and two path nodes each. Teleporting onto gate 0's first node puts Spyro about 1,000
view units from the portal, which the seeker closes to 696 — the walk alone stalls at 11,272 because
the portals sit above the hub and the steering loop cannot climb.

At that range the frame refuses by name, as designed:

    [fieldsky] REFUSED status=invalid portal recipe reason=portal_recipe portals=5 active=1 valid_empty=0

That was retail's `s1 == 2` case: the aperture qualifies, no point is on screen, and the portal is
inside `0x1000`.

`RotVec8ToMatrix` (`0x80016D2C`) is what it needs, and it is now ported as
`cyclorama_portal_mesh::rotVec8ToMatrix`. Read: it is GTE-based, starting from the identity (or a
caller-supplied base in `a2`) and composing one axis at a time on the RIGHT with `MVMVA 1,0,0,3,0`,
taking each angle byte as a `*2` index into the sine table `D_8006CBF8` and the cosine table
`D_8006CC78` (the same `kSineTable` the port already uses, offset 0x80, so a byte `b` is exactly the
entry the port's interpolating helpers return for `b << 4`). The order is yaw about Y, then pitch
about X, then roll about Z, and a zero byte skips its rotation. Do NOT reuse `portalMatrices`' X*Y*Z
composition for this: it is a different guest routine and the order is not the same. A unit test
pins the order by checking the forward vector at quarter turns, where each axis gives a different
answer.

The caller at `0x80051D98`-`0x80051E6C` feeds it the camera's three angles at `g_Camera+0x4C/0x4E/
0x50`, each `>> 4`, rotates `(0x1000, 0, 0)` by the result — which, for a 1.12 fixed-point matrix,
is just its first column — and drops the portal when the dot product with `worldCentre - camera` is
negative. `worldCentre` is the portal's WORLD centroid, which retail builds at `0x8005102C` by
nulling a vector, adding every portal point and dividing by the count; `prepareFrame` now
accumulates it in the same projection loop.

Measured after this: the same walk reports **zero** cyclorama refusals, and the port runs through
portal entry into level loading. Two further defects were behind it, both now fixed:

`executor-pc=0x80063D80` is **CdControlF**, not CdControlB — external/spyro-1's `asm/psyq.s` labels
`CdControl` 0x80063C48, `CdControlF` 0x80063D80 and `CdControlB` 0x80063EAC. CdControlF takes only
`(com, param)`, so at its call sites a2 holds the caller's leftover register; the plan named that
address kCdControlB and bound it to the result-writing owner, which wrote 8 bytes at 0x09B30000. The
framework now has `cd_control_fire_sync` for the no-result entry, and the title binds all three
addresses to the right owners. The real CdControlB had been bound to nothing at all.

Adding the ninth binding then exposed that psxport's direct-runtime binding loop clamped to
`kMaxBindings` and silently dropped the rest, which would have left a hardware entry executing guest
code with no owner. It refuses by name now.

With both fixed the portal is crossed and the level load runs to **stage 1 (GS_LevelTransition),
load stage 2**, where the render stops on its own honest boundary: no native producer is registered
for that stage. That is the next porting task, and it is the same screen the Start-cancellation work
targets.

## A test file that had never been compiled

`tests/test_cyclorama_portal_mesh_recipe.cpp` existed but was in no `add_test` list, so neither it
nor the two visibility tests added with `meshVisibility` had ever run. It is registered now and runs
with the rest. A test that is not in the suite is not coverage, and nothing reported its absence —
the suite counted 30 tests and none of them was this one.

## What is still open

The mask. Retail builds its aperture from the UNCONTRACTED points with a sub-3 segment collapse
(loop 1) and the meshes' from the contracted points without it (loop 2); the port has only loop 2
and gives it to both. Also, the port derives its clip rectangle before the contraction where retail
recomputes it after, and it widens the segment test to the widescreen frame where retail uses 0x200.

## The orientation discriminator was run## The orientation discriminator was run, and it is inconclusive here

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

## Retail builds the aperture twice per portal — corrected reading

Read end to end from `func_80050BD0.s`, per portal:

1. the points are projected into `D_80078DD8` (X), `D_80078DDC` (Y), `D_80078DE0` (Z), stride 12;
2. edge loop 1 fills `D_80077EA0` from those UNCONTRACTED points, with a collapse for any segment
   whose `|dx|` or `|dy|` is under 3 (`0x80051410`, `0x8005142C`), then `jal func_8004FEA0` at
   `0x800518C4` draws the MASK;
3. `0x80051934`-`0x800519E4` contracts every point by ±2 toward the centroid IN PLACE, rewriting
   `D_80078DD8`/`D_80078DDC`, and recomputes the bounding box from scratch with the extents
   initialised to `0`/`0x200` and `0`/`0xF0`;
4. edge loop 2 refills `D_80077EA0` from the now-contracted points, with no sub-3 collapse, and
   accepts a segment unless both endpoints are off the same side (`X < 0x200`, `X > 0`, `Y < 0xF0`,
   `Y > 0`); then `jal func_80050240` and `jal func_8004F4BC` draw the far and near MESHES.

An earlier revision of this issue had the two loops the wrong way round. The port's single edge loop
is loop 2: it runs after the contraction, has no sub-3 collapse, and its `crossesScreen` test is
loop 2's both-endpoints-off-one-side test. So the mesh aperture is the one the port transcribed, and
it is the MASK that is being handed the wrong list — a real defect, but not this one, and consistent
with the mask still rendering.

Two smaller divergences fall out of the same read and are worth fixing with it: the port derives its
clip rectangle from the points BEFORE the contraction, where retail recomputes it after; and retail
tests the segment against `0x200`, where the port widens to the widescreen frame.

So the near mesh's emptiness is NOT explained by aperture provenance. What remains is either that
zero accepted faces is the correct answer for this pose and the submitter must not abort on it, or a
fault in the projection feeding both. Note the pose is degenerate — the aperture centroid projects to
`(-283,-205)`, off screen — so the projection is the first thing to rule out.

## Next discriminator

Compare the two poses directly: dump `frame.edges`, the clip rectangle, and the projected aperture
points for the teleport frame that emitted 94 triangles and for this walked frame that emitted 0.
If the walked frame's half-planes enclose no screen area, the aperture construction is the fault; if
they enclose area the geometry misses, the near family's clip source is, and what `0x8004F4BC`
actually clips against must be recovered from the retail routine rather than from the mid/far
transcription. `0x80050BD0` selects between the two families at the same `0x3000` threshold, so the
selection itself is already recovered.
