---
id: 120
title: User reports gems drawing on top of terrain that should occlude them
status: open
related: 0105 (actor-vs-actor depth order, 5.07%). NOT established as the cause of this issue: that instrument walks retail's moby list and cannot see terrain. Both stay open until one measurement connects them.
symptom: PARTLY FIXED 2026-09-19 — the missing world ordering bin is recovered and verified (415/415 authored bins match retail, r=+1.0000 with range), but the VISIBLE symptom is unchanged because the GPU depth test uses per-vertex pzToOrd(viewZ), which is degenerate across range (0.022290 vs 0.022285 for ranges 15197 vs 8336). "Spyro looks like gems that should be behind terrain rendering on top" (user, 2026-09-19) — REPRODUCED at the framebuffer: at f2400 of artisans-arrival, 116 px show a violet object in the native where the console shows grass (reverse direction only 41 px). Separately, retail's moby walker shows a 5.07% actor-vs-actor depth-order disagreement, which is issue 0105; that instrument cannot see terrain and does not evidence this symptom
state_items: S019, S020
tags: render,depth,occlusion,oracle
created: 2026-09-19
---

## The report

USER, 2026-09-19: "Spyro looks like gems that should be behind terrain rendering on top."

This is a user-observed regression against the running product and outranks every contradictory
local measurement below. Nothing here argues it away; what follows is only which explanations have
been RULED OUT, so the next session does not re-derive them.

## What the depth contract actually is

`external/psxport/runtime/psx/render_queue.h`: the D32 depth buffer resolves occlusion WITHIN
`RQ_WORLD` only, and `gpu_vk.cpp` sets `enable_depth_test = true` with
`SDL_GPU_COMPAREOP_GREATER_OR_EQUAL` for every world pipeline. So among world prims that carry
depth, the nearer one wins, and there are only three ways a gem can beat terrain:

1. it is not on `RQ_WORLD` / not `RQ_OM_DEPTH`, so it is painted over the world unconditionally;
2. its depth VALUE is wrong relative to terrain's; or
3. the terrain is not drawn there at all, and something further back shows through.

## FALSIFIED: authored OT order overriding the depth buffer

`RenderQueue::resolveKeyOrderFaces` can replace a keyed face's depth with its guest OT bucket
(`FACE_ORDER_AUTHORED`), which would put gems and terrain on two different scales. It does not run
here. Measured over the full `replays/gameplay/artisans-arrival.pad` route with `PSXPORT_DEBUG=keyord`:

```
f3605 [flush] queue census: 1747 prims = 0 keyed faces + skipped(painter 1747, non-world 0,
                            non-depth-order 0, NO SORT KEY 0, no object 0)
f3605 [flush] resolveKeyOrder: 0 keyed faces of 1747 queued prims — nothing to contest
```

Every prim carries a painter object, so the contest is skipped by construction, and Spyro declares
`FACE_ORDER_DEPTH` as its default anyway (`game/core/spyro_runtime.h` takes the
`RenderCapabilities` default). The census names its own denominator, so this is "scanned and found
none", not "never ran".

## FALSIFIED: producers normalising depth against different near planes

`ProjParams::pzToOrd` normalises against `projNearPz() == projH/2`, and `projH` is per-frame state a
producer may install for the length of its own submission. Three Spyro producers reach it three
different ways — `terrain_submitter.cpp`'s `ProjectionPlaneScope`, `world_scene_submitter.cpp`'s
own local `ProjParams`, and everything else through the shared `core->rsub.projParams` — so a frame
in which they disagree would put their faces on different depth scales in one shared D32 buffer.

`ProjParams::setProjH` now reports every transition on channel `projplane` (the single owner of that
state, so there is one call site and no per-producer copy). Over the same full route, 316,623
transitions:

| transition | count | what it is |
|---|---|---|
| `341 -> 341 (unchanged)` | 313,513 | every producer installing the SAME plane |
| `0 -> 341 CHANGED` | 2,400 | a freshly constructed local `ProjParams` taking that same plane |
| `0 -> 0 (unchanged)` | 710 | before the game has set a plane at all (boot/title) |

One plane, frame-wide, for the whole of gameplay. The instrument has shown both answers — it prints
`CHANGED` and `unchanged` from the same call site — so the negative is real.

## REPRODUCED 2026-09-19 — and the console does not draw the object at all

`tools/picture_oracle.py --route replays/gameplay/artisans-arrival.pad --route-from 1830 --play 4951`
drives BOTH cores with the same recorded pad from the `playing` checkpoint, which is the same-state
gameplay comparison issue 0119 said was missing. At the end of the route the two pictures are
recognisably the same moment — same dragon, same pose, same camera — so the route alignment holds.

Stepping along that route (`--frame-step 400`, which had to be implemented: picture mode accepted
the flag and ignored it) finds the defect. At the courtyard, the port draws small dark-red quads on
the face of the left hedge. Red-pixel clusters, product vs reference, same frame:

| frame | product clusters (x, y) | console clusters (x, y) |
|---|---|---|
| f400 | **167,105 · 188,108 · 218,113** · 251,108 · 262,108 · 251,116 · 260,116 · 250,124 · 261,124 | 255,104 · 250,112 · 259,112 · 250,120 · 260,120 |
| f800 | **170,106 · 186,109 · 220,114** · 259,110 · 273,110 · 247,116 · 264,118 · 247,124 · 263,126 | 211,124 · 293,125 · 219,126 · 227,126 · 238,128 · 276,128 · 284,128 · 261,130 |

The bold clusters are the defect: they hold position within 2px between f400 and f800, so they are
STATIC WORLD OBJECTS and not an animation phase difference, and **the console has no red pixel
anywhere left of x=211 in either frame**. The rest of the red in both cores is Spyro's own wings.

## What the pixel probe says the winner is

`PSXPORT_PRIMAT=50,95,3595` over the same replay, at the rightmost of those quads:

```
f3605 seq=738  layer=1 om=0 semi=0 nv=4 mode=3  interp=0.093418  D32=0.144536  <- WINS
f3605 seq=844  layer=1 om=0 semi=0 nv=4 mode=0  interp=0.020983  D32=0.081197
f3605 seq=910  layer=1 om=0 semi=0 nv=4 mode=0  interp=0.022843  D32=0.082852
f3605 seq=994  layer=1 om=0 semi=0 nv=4 mode=0  interp=0.026331  D32=0.085938
f3605 seq=1032 layer=1 om=0 semi=0 nv=4 mode=0  interp=0.030166  D32=0.089308
f3605 seq=1069 layer=1 om=0 semi=0 nv=4 mode=0  interp=0.038619  D32=0.096719
f3605 seq=1093 layer=1 om=0 semi=0 nv=4 mode=0  interp=0.049404  D32=0.106166
```

So mechanism (1) is **ruled out too**: the quad is on `RQ_WORLD` with `RQ_OM_DEPTH`, exactly like
the terrain it beats. It wins because its depth says it is nearer, and the depth test is working.

Everything else covering that pixel is textured (`mode=0`); the winner is `mode=3`, untextured flat
— which is what `field_shaded_queue_submitter` emits, the producer that draws Spyro's class-83 gem
instances (issue 0111). Inverting `pzToOrd` at the measured plane (projH 341, near 170.5): the quad
is at view-Z **~1,829** and the hedge behind it at **~8,100**. It is not marginally in front, it is
four times closer to the camera.

## The reading this does NOT settle

A quad four times too close would be a TRANSFORM defect, not a depth one — and that is not the same
claim as the user's. Two readings remain open and the evidence above does not choose between them:

1. **Wrong position.** The gem's world/view transform puts it near the camera while its screen XY
   still lands up on the hedge, so it paints over terrain it is genuinely in front of, in a place it
   should not be. Note the size is consistent with the near depth (H/pz at 1,829 makes a ~40-unit
   gem about 7px; the quads measure ~4x2), so sx/sy and pz are not obviously disagreeing.
2. **Should not be drawn at all.** The console draws NOTHING there — not in front, not behind. If
   the game culls or deactivates that record and the port's shaded-moby recipe admits it anyway,
   the symptom is a visibility defect that merely looks like a depth one.

`dbg_node` is `00000000` for every one of these prims, so nothing attributes them to an owning
guest object, which is why neither reading can be decided from this capture. Attributing the
shaded-moby producer's prims to their record is the next step, not more depth measurement.

## Not yet established

- **Which guest record these quads are.** `dbg_node` is 0 for the shaded-moby producer's prims, so
  they are unattributed. Until they carry their record, neither reading above can be decided.
- **Whether the same defect accounts for what the user sees elsewhere.** One courtyard is not the
  game; the report may cover more than these three quads.
- The pale fragments on the grass that this issue first chased are **a cobbled path present in both
  cores** — not a defect. The probe at (53,155) agreed: an ordinary textured world triangle at
  D32=0.275996 beating a quad at D32=0.130047, the nearer prim winning.

## Next step

1. Give the shaded-moby producer's prims their owning record as `dbg_node`, so the probe names the
   object instead of `00000000`.
2. With the record in hand, compare its recovered world position and its admission gates against
   `r_moby.s` / the guest's own walker (`PSXPORT_ACTOR_SCENE_ORACLE=1`, issue 0111) — that oracle
   runs retail's own moby walker over the same state and is the instrument that can say whether
   retail submits this record at all, which is exactly the question reading (2) asks.
3. Do NOT change depth, ordering or layer policy to make the quads disappear. The depth buffer, the
   layer, the order mode and the near plane have each been measured correct here; a change there
   would hide a wrong position behind a wrong occlusion rule.

## CORRECTION 2026-09-19: the probed prim is NOT the gem producer

An earlier revision of this issue attributed the untextured quad that beats the hedge to the
shaded-moby queue (`field_shaded_queue_emit::kProducerKey` = `0x80022a2c`, the class-83 gem producer
of issue 0111). That attribution was a guess from the prim's shape (`mode=3`, `nv=4`) and it is
wrong.

The pixel probe now prints `painter=`, the producer key every native submitter already opens its
`RenderQueue::PainterObjectScope` with (`external/psxport/runtime/psx/gpu_debug.cpp`). Every prim
covering the probed pixel — the untextured quad AND the textured hedge faces it beats — carries
`painter=800258F0`, which is `spyro::world_temporal::kProducerKey`: the WORLD SCENE submitter
(`game/render/world_scene_submitter.cpp`). The gem producer submits nothing there.

That also settles the `dbg_node=00000000` question this issue previously left open. It is not a
missing `beginObject` scope in the shaded queue; the prims simply do not come from a producer that
opens one. `world_scene_submitter` opens a painter scope and no diag object scope.

## FALSIFIED: the artefact is a flat-shaded (vertex-coloured) prim

`PSXPORT_PRIMRGB` (new, `external/psxport/runtime/psx/prim_color_census.{h,cpp}`) censuses every
prim in a frame carrying a given vertex colour, and always reports its denominator. At the Artisans
courtyard frame:

```
[primrgb] f3595 scanned 1736 prim(s), 12 carried rgb(120,0,0) +-30
```

All 12 are `painter=8001F798` (`fx_actor_draw::kProducerKey`) with display bounding boxes inside
x 294..350, y 68..80 — the NPC/dragon cluster on the right of frame, which is legitimately
red-brown. NONE of them covers any of the dark-red dots the report is about. So whatever paints
those dots is textured, not vertex-coloured, and a vertex-colour search cannot find it.

## The capture used for coordinates is NOT the frame the queue drew

This is the blocker, and it invalidates every coordinate-based conclusion taken from a
`PSXPORT_PRESENT_SHOT_AT` PNG.

The pixel probe now reports the display rect it resolved the target against, and it is
`display=(0,240)+512x240` — Spyro renders 512 wide, not 320, so any mapping that assumed a 320-wide
framebuffer was reading the wrong column. With the 512x240 mapping the probe's colour answer agrees
with the capture on broad regions:

| pixel | probe `shaded=` | capture |
|---|---|---|
| grass (256,200) | (69,143,59) | (74,140,49) |
| sky (256,20) | (247,195,250) | (255,197,206) |

but disagrees completely at the artefact: probe (170,141,120) tan against a capture pixel of
(107,0,0) red.

The new row probe (`PSXPORT_QROW`, `external/psxport/runtime/psx/queue_row_probe.{h,cpp}`) reports a
whole row of the queue's own per-pixel answer so a capture can be diffed against it BY CODE. At
`y=94, x=20..110` the queue paints a uniform tan band owned by two prims (`seq 727`, `seq 735`,
both `painter=800258F0`), 91 of 91 pixels covered. The captured row is green hedge with red dots.

Sweeping that row over **1884 frames** of the route and scoring each against the captured row gives
a best mean per-channel error of **100** (at f3388). There is no frame skew that reconciles them:
the queue never draws the captured row. So the capture and the queue are not describing the same
picture, and the fault is in the capture path or its coordinate frame, not in a frame index.

`PSXPORT_PRESENT_SHOT_AT` is also flagged by the port's own configuration audit
(`[cfg:warn] UNKNOWN knob PSXPORT_PRESENT_SHOT_AT ... it did NOTHING in this run` — printed even on
runs where it DID write a PNG, because it is on the legacy `cfg_*` path), and it writes to
`scratch/screenshots/` rather than the run's output directory. Treat its PNG as untrusted for
coordinates until that path is reconciled with the render queue's frame and display rect.

## Next step

1. Get the product picture from the trusted path instead: the picture oracle's own product PNG
   (`tools/picture_oracle.py`, which captures through the product REPL's `shot`), and establish the
   frame it corresponds to, so a coordinate read off it can be fed to `PSXPORT_PRIMAT`/`PSXPORT_QROW`.
2. Only then re-open the occlusion question. The depth buffer, the layer, the order mode and the
   near plane have each been measured correct here (see the falsified sections above); do NOT change
   depth, ordering or layer policy to make an artefact disappear.

## REPRODUCED against the console, in the trusted capture path

`tools/picture_oracle.py` captures both cores' framebuffers at 512x240 — the framebuffer itself, not
a presentation upscale — so a coordinate read off one of its PNGs needs no mapping at all. Comparing
`scratch/picture/artisans-arrival-4951f-f2800.{native,console}.png` (route
`replays/gameplay/artisans-arrival.pad`, `--route-from 1830 --play 2800`), censusing pixels with
`r > 70, g < 45, b < 45`:

| | product | console |
|---|---|---|
| clusters | 18 | 26 |
| present only in the product | **(168,106) = (144,0,0)** and **(220,113) = (208,32,32)** | — |

Both cores agree on the NPC/dragon reds at (330,72)/(294,76) and on the Spyro-body cluster around
x 242..269, y 103..126, so the census is not simply finding different things in two different
pictures. The console additionally carries a scatter of dull reds across the right-hand grass that
the product does not, and shows a gold gem sparkle near (325,128) that the product does not — those
are separate deltas, not this one.

The two product-only pure reds are the artefact the report is about, and they are now REPRODUCED in
the capture path whose coordinates can be trusted.

## What the side-by-side actually shows: gems in the WRONG PLACE, not gems in front of terrain

Zooming both captures on x 140..350, y 85..150 (`scratch/picture/zoom_compare.png`, product above,
console below) makes the shape of the defect plain, and it is not an occlusion failure:

- The product draws **three faceted red gems lying on the courtyard grass** to the left and centre.
  The console draws **none** there.
- The product draws a **gold gem floating high beside the dragon statue**, inside the statue's glow.
  The console draws its gold gem **on the ground at the lower right**, with its sparkle trail.

Both cores show Spyro at the same screen position and pose and the same camera, so this is not the
two cores having diverged into different world states. The gems are being PLACED wrongly: the same
records, drawn at the wrong world positions.

That reframes the user's report. "Gems that should be behind terrain rendering on top" is what a
mis-placed gem looks like from the player's seat — a gem whose recovered position puts it in front
of geometry it belongs behind. The depth buffer is doing exactly what it is told with the position
it is given; the position is what is wrong. This is consistent with issue 0111's recorded residual,
that a class-83 gem's TRZ is approximated by the actor's view-Z origin rather than resolved.

So the fix belongs in the gem record's transform recovery, NOT in depth, layer, or ordering policy.

## The candidate, and how to decide it

Issue 0111 established that exactly **three `class 83` gem records** are accepted by both the native
producer and retail's own `func_80022A2C` at this scene — `0x8016D6A8`, `0x8016D700`, `0x8016D758`,
list ordinals 0, 1, 2 — and that the record GATE never disagreed between them. The product draws
three red gems here. The console draws one gold gem at the lower right and nothing where the
product's three are. Three accepted records and three drawn gems on one side, differently placed on
the other, is the shape of a transform defect, not an admission defect.

The transform is `spyro::actor_transform_math::worldAffine`
(`game/render/actor_transform_math.cpp:138`), which composes:

- `cameraRelativePosition` — note it is deliberately asymmetric, `moby - camera` on X and
  `camera - moby` on Y and Z, encoding the guest's own axis orientation;
- an `IR = (Y, Z, X)` reorder before the camera matrix, matching `0x80022A2C`;
- `rotateForMoby` from `moby + 0x44`;
- `scaledTranslation` with `moby + kMobyScaleByte`, which deliberately keeps the guest's 16-bit
  `mtc2` truncation so a far actor wraps exactly as it does on hardware.

Every one of those steps is shared with Spyro himself and the NPC actors, which the side-by-side
shows in the RIGHT places, so a blanket error in the shared path is ruled out by that alone. What is
NOT shared is whatever is specific to the class-83 record — its scale byte, its `0x44` rotation
word, or the recipe's approximation recorded at `game/render/field_shaded_queue_recipe.cpp:231`
("TRZ is approximated by the actor's view-Z origin").

Decide it by measurement, not by reading: `PSXPORT_ACTOR_SCENE_ORACLE=1` runs retail's own moby
walker in-process over the same state (issue 0111). Compare, for each of the three gem records, the
native's computed view position and projected screen XY against retail's, and report all three with
their record addresses. One record disagreeing and two agreeing, or all three disagreeing by the
same transform, each point at a different cause; a single aggregate number cannot tell them apart.

## Who paints red on the gem row: measured, with a denominator

`PSXPORT_QROW=106,150,190,0` armed through the picture oracle's `--product-env` over the full
`--route-from 1830 --play 2800` run, then every reported row scanned for a pixel with
`r > 70, g < 45, b < 45`:

```
scanned 10615 row report(s) at y=106; 72 red pixel(s)
producers painting red on this row: 8001F798, 80023AC4
```

- `0x8001F798` is `spyro::fx_actor_draw::kProducerKey` — the ordinary actor draw.
- `0x80023AC4` is the PAIRED-ACTOR renderer (`game/render/fx_paired_actor.cpp`, whose alternate
  status-plane arm is still a loud refusal).

`0x80022A2C`, the shaded-moby queue that issue 0111 established as the gems' producer, paints
**nothing red on this row in 10,615 frames**. So either the gem faces are not red where the capture
shows red, or the gems on this row come from the paired-actor renderer rather than the shaded queue.

This is a denominator, not a verdict: the row is one scanline, and the captured frame is not
guaranteed to be among the 10,615 reported (the row probe reports per render-queue frame, the
capture is taken at the end of the play segment). Do not read it as "the shaded queue is innocent".
Read it as: the next measurement should be `tools/actor_oracle_diff.py` on a
`PSXPORT_ACTOR_SCENE_ORACLE=1 PSXPORT_ACTOR_SCENE_ORACLE_CLASS=83` run, which matches native and
retail primitives by geometry and NAMES the producer of every unmatched one — exactly the question
this row scan can only narrow.

## DECIDED by retail's own walker: the geometry is right, the DEPTH ORDER is wrong

`tools/actor_oracle_diff.py` on a `PSXPORT_ACTOR_SCENE_ORACLE=1 PSXPORT_ACTOR_SCENE_ORACLE_CLASS=83`
run over `replays/gameplay/artisans-arrival.pad` (220 oracle frames parsed, reporting the last):

```
native primitives : 692     retail primitives : 670
matched           : 664     retail only : 4     native only : 28
measured best horizontal shift +0

native primitives by producer (submitted / matched / recoloured / unmatched):
  0x8001F798: 357 / 357 / 0 / 0        0x80020F34:  75 /  75 / 0 /  0
  0x80022A2C:  41 /  39 / 2 / 0        0x80023AC4: 173 / 173 / 0 /  0
  0x800580F4:   8 /   8 / 0 / 0        0x800584C4:  10 /   0 / 0 / 10
  0x80059A48:  16 /  12 / 0 / 4        0x80059F8C:  12 /   0 / 0 / 12
```

**The gem producer is not misplacing anything.** `0x80022A2C` submits 41 primitives, 39 match
retail's own walker by geometry with zero unmatched, and the 2 "recoloured" are off-by-one colour
words (`retail=000067 -> native=000066`, `retail=0000A0 -> native=00009F`) — a rounding difference,
not a visible defect. The gems in this frame are blue (`0x000066`, `0x00009F`) at screen (146,114)
and (164,121), where retail puts them.

This **contradicts the "wrong world positions" reading** in the section above, which was taken from
the picture comparison alone. Both measurements stand; they are of different moments (the capture is
the end of the `--play` segment, this diff is the last oracle frame of a straight replay). The
oracle's is the stronger evidence about placement because it compares against retail's own walker
primitive by primitive, so treat "gems are in the wrong place" as NOT established.

**What IS established is a depth-ORDER disagreement, and it names a gem:**

```
depth agreement over matched primitives:
  comparable ordered pairs : 174417
  measured orientation     : a larger OT bin means a smaller native depth
  disagreeing with retail  : 8850   (5.07%)
  bins 171 vs 105 (span 66) but native depth 0.012609 vs 0.009528
       (8016F0C8 class 83 scale 0 dist 26333  /  8016FA10 class 10 scale 0 dist 17518)
```

Retail bins the `class 83` gem at 171 and the `class 10` object at 105, and the tool measured that a
larger bin means a smaller native depth — so retail wants the gem to carry the SMALLER depth of the
two. The native gives it the LARGER (0.012609 vs 0.009528). The world pipeline compares
`GREATER_OR_EQUAL`, so a larger depth wins the pixel: **the native draws this gem in front of an
object retail draws it behind, and the gem is the farther of the two (dist 26333 vs 17518).** That
is the user's report, reproduced against retail's own ordering rather than against a screenshot.

The likely cause is already written down at `game/render/field_shaded_queue_recipe.cpp:231`: "TRZ is
approximated by the actor's view-Z origin." A gem's depth comes from its actor origin rather than
from the value retail's pass computes, so it sorts on a different quantity from the objects it has
to sort against. Recover TRZ properly for this producer and re-run this diff; the disagreement rate
and this named pair are the falsifier.

### Separately measured, not this issue

Three producers emit primitives retail does not, at the same frame: `0x80059F8C` (12 of 12
unmatched — `class 114` and `class 10` semi-transparent textured quads), `0x800584C4` (the sparkle
arm, 10 of 10 unmatched, all white lines) and `0x80059A48` (Spyro's shadow, 4 of 16). Retail-only is
4 primitives, all `code 32`, semi-transparent and untextured. Each of these deserves its own issue
rather than being folded into this one.

## The mechanism, named: the producer computes retail's order and then throws it away

`game/render/field_shaded_queue_recipe.cpp` computes the gem's OT bin exactly as retail does — the
sum of the four vertices' `sz`, MINUS an actor-origin bias, PLUS a reverse-facing term:

```cpp
int64_t depth = projected[i0].sz + projected[i1].sz + projected[i2].sz + projected[i3].sz;
depth -= (int64_t)std::max(record.affine.t[2] - 256, 0) * 4;   // actor-origin bias
if (reverseFacing) { depth += 512; }
const int64_t ot = depth >> 5;
```

`game/render/field_shaded_queue_submitter.cpp` then submits those faces as `RQ_WORLD` /
`RQ_OM_DEPTH` with `depth[i] = pzToOrd(face.vertices[i].viewZ)` — raw per-vertex view-Z. **Neither
the actor-origin bias nor the reverse-facing term is present in the value the depth buffer actually
compares.** The producer computed retail's ordering answer and then submitted a different quantity.

That is a sufficient explanation for the measured disagreement, and it explains why it shows up as a
gem-versus-object contest specifically: the bias is proportional to the actor's own view-Z origin
(`affine.t[2]`), so the further an actor is from the camera the more its true order diverges from
its raw vertex depth — and the named pair is a gem at dist 26333 losing to an object at 17518.

Two things this is NOT, and must not become:

- It is not a licence to add a fudge to the gem's depth until the artefact disappears. The bias is
  a specific recovered quantity with a specific scale; either it is carried into the submitted depth
  correctly or the face carries its authored order instead.
- It is not the near-plane theory (falsified above, 316,623 transitions, one plane frame-wide) and
  not the authored-OT-contest theory (falsified above, 0 keyed faces of 1747).

The framework already owns the alternative: `RqItem::authored_depth` / `FACE_ORDER_AUTHORED` and
`sort_key` exist so a producer that knows the guest's own order can submit it instead of a depth.
The earlier census showed 0 keyed faces here because every prim carries a painter object, so taking
that route means deciding how the painter-object path and the keyed-face path compose — a design
decision, not a patch.

**Falsifier for whichever route is taken:** re-run `tools/actor_oracle_diff.py` on a
`PSXPORT_ACTOR_SCENE_ORACLE=1 PSXPORT_ACTOR_SCENE_ORACLE_CLASS=83` log and require the
disagreement rate to fall from 5.07% and the named pair (`8016F0C8` class 83 vs `8016FA10`
class 10, bins 171 vs 105) to agree. Anything that hides the artefact without moving that number is
a bandaid.

## 2026-09-19: the symptom REPRODUCED AT THE FRAMEBUFFER, and the scope error in the claim above

The "REPRODUCED against retail's own walker" line in this issue's `symptom:` overstated what that
instrument can see. `tools/actor_oracle_diff.py` walks retail's MOBY LIST. It cannot see terrain at
all, so it can establish an actor-vs-actor depth-order disagreement (it did: 5.07%, issue 0105) and
it can NOT establish anything about a gem drawing over terrain. Those are two claims and only one of
them had evidence.

The framebuffer comparison supplies the missing one. Paired 512x240 picture-oracle PNGs of
`replays/gameplay/artisans-arrival.pad`, native vs console, same frame, no upscale:

```
predicate: saturated blue/violet, b>70 and b>r+40 and b>g+40
frame   both  native-only  console-only
 f400     32       42         42
 f800     23       58         42
f1200     23       43         32
f1600     22       86         35
f2000     32       45         28
f2400     20      116         41     <-- worst
f2800     31       46         36
f3200     28       50         31
f3600     31       42         36
f4000     22      110         47     <-- second
f4400     50      114        107
f4800     22       38         39
```

The predicate is not stuck on one answer: `console-only` is nonzero on every frame and EXCEEDS
`native-only` on f4800, and both counts are 0 on `playing.png` and `selftest-before.png`. It reports
the other answer when the other answer is true.

At f2400, the 116 native-only pixels span x139..296 y24..149, and the two sides disagree by object,
not by brightness:

```
what NATIVE draws there : (104,80,152) (96,72,144) (120,96,176) (128,112,176)   violet
what CONSOLE draws there: (56,128,48)  (56,120,48)  (72,136,56)  (72,128,48)    grass
```

That is the user's report, measured: an object the console hides behind terrain is visible in the
native. It is asymmetric (116 vs 41), so it is not a sub-pixel geometry shift — a shift produces
balanced counts, which is what the quiet frames show.

### What this does NOT say

The whole-frame component analysis of f2800 found 1398 connected regions differing by >60, and the
five largest are all shading, not occlusion: means like native (158,184,130) vs console (121,152,115)
— same hue, different brightness — and one region whose two means are (151,114,78) vs (151,114,77),
i.e. pure edge noise from a sub-pixel shift. Do not read the 10.4% whole-frame difference rate as
occlusion error; almost all of it is something else.

### Falsified here: a depth-unit mismatch between terrain and the shaded queue

Every `pzToOrd` call site in `game/render/` was enumerated (17 of them). `terrain_submitter.cpp:113`
and `field_shaded_queue_submitter.cpp:65` both use the live shared `core->rsub.projParams` on raw
per-vertex `viewZ`. They are the same unit, so gem-vs-terrain cannot be explained by a projection
scale difference. `world_scene_submitter.cpp` is the ONLY site that does not use the live shared
params — it snapshots `projH` at prepare time (line 110) into a local `ProjParams` at emit time
(143-144). That divergence is only possible if `projH` changes between prepare and emit, which is
unmeasured and is the next thing to check.

### Next discriminator

Probe one of the f2400 native-only violet pixels with `PSXPORT_PRIMAT` to name the painter, then
ask whether terrain submitted any covering primitive at that pixel with `PSXPORT_QROW`. If terrain
submitted nothing there, this is missing terrain, not a gem sorting in front of it, and the cause is
in a different producer entirely. Designing that negative first matters here: "no terrain prim
covered this pixel" and "terrain covered it and lost the depth test" look identical on screen.

## 2026-09-19: the row probe answered the "did terrain even cover it" question

The discriminator named above was run. `PSXPORT_QROW=103,236,254` and `PSXPORT_PRIMAT=244,103` over
the product, 2,666 reported frames at that row, 18,256 pixel-probe lines.

The instrument reports the other answer, so its positives mean something: of the 2,666 rows, **191
reported `0 of 19 pixel(s) covered`**, two reported partial coverage (12 and 15 of 19), and the rest
reported 19 of 19. It distinguishes "nothing in the queue covered this pixel" from "something dark
covered it" — the uncovered case prints `------`, never black.

Who wins that row, over every reported frame:

```
  29737  800258F0   world scene
   8131  8004EBA8   terrain
   3619  00000000   NO PAINTER OBJECT AT ALL
   3187  8001F798   actor draw
   2284  800573C8   field particles
     56  80023AC4   paired actor
```

So terrain and the world scene do cover this row, in quantity. The answer to "is terrain missing
there" is no. Actors do win 3,187 pixels at this row across the run; whether any of those wins is
wrong is still not established, because this run had no matched console frame to compare against.

At the one frame the final pixel probe reported, the winning primitive at (244,103) is green
terrain, correctly:

```
[primat-rq] FINAL f5063 @(244,103) display=(0,240)+512x240 compare=GREATER_OR_EQUAL
  shipping(valid=true order=1314 seq=759 node=8016D758 D32=0.150659949 texel=1E06 writes=true)
[qrow] f5063 y=103 x=236..254 scanned 3891 prim(s), 19 of 19 pixel(s) covered
  colours: 4B8F40 4C8F40 4C9048 419037 419038 379140 379140 429138 ...   (all greens)
  owners:  755@800258F0 x19
```

### Two things this run surfaced that are not this issue

**3,619 pixels are won by a primitive whose painter object is 0x00000000.** That is 7% of the wins
at this row. A primitive winning the depth test with no recorded painter cannot be attributed to any
producer, which makes it invisible to every producer-keyed instrument including the actor oracle.
Worth its own issue; it may also be why some producer censuses do not add up.

**`node=8016D758` appears on primitives whose painter is `800258F0`.** That node is one of the three
class-83 GEM records from issue 0111, but the painter is the world scene. Every one of the 19 pixels
in the final row carries that same node with that same painter. Most likely `diag.beginObject` is
not cleared when the shaded queue finishes, so later world-scene primitives inherit the last gem
node. If so, `node=` is unreliable for attribution and any earlier reading that leaned on it should
be re-checked — including this issue's own first, corrected, attribution mistake.

### The run did not complete, and that matters

It died at frame 5064 with `NATIVE RENDER NOT IMPLEMENTED — stage selector = 2`, recorded against
issue 0103. The requested `--route`/`--play` never drove the comparison; the oracle fell back to its
scripted checkpoints. The probe data above is still good — those frames really were rendered and
really were probed — but this run produced no matched native/console pair at the coordinate, so it
cannot close this issue.

## 2026-09-19: the 800258F0 mechanism note in 0105 was retracted

The hypothesis this issue contributed to 0105 -- that the shaded queue's submitter drops the
actor-origin bias -- is false and is retracted there. The submitter passes `queuedWorld(face.otBin,
face.paintGroup)` on every face and `prepare()` refuses the plan if any face's order is not authored.

The reading that produced it came from this issue's own probe output: `authored=0` on all 15,592
probed lines was taken to mean "no authored order", when `authored_depth` is a statement about the
depth ARRAY. The field that carries retail's bin, `RqItem::painter_replay`, was not printed at all.
The probe now prints `replay_domain/replay_ot/replay_link/replay_sub` and the misleading field is
renamed `authored_depth=`.

This is the second attribution mistake in this issue, after the `painter=` correction earlier. Both
had the same shape: a probe field was read as answering a question it does not answer. Treat any
remaining claim here that rests on a single probe field as unconfirmed until the field's own
definition has been checked in `render_queue.h`.

## 2026-09-19: CAUSE LOCATED — the origin bias cancels the range, and inverts the order

Measured on 7,127 shaded-moby faces from one `--seek-class 83` gameplay run
(`PSXPORT_DEBUG=shadedface`, the per-face line now prints each term of the OT arithmetic):

```
  szsum vs t2   : r=+0.9999     the sum of the four projected sz tracks range, as it must
  depth vs t2   : r=-0.2747     what survives the bias does NOT -- it runs BACKWARDS
  szsum range   : 1689 .. 48513   spread 46824
  depth range   :  525 ..  1938   spread  1413      (33x compression)
  ot    range   :   16 ..    60   of 288 bins -- 15.3% of the table ever used
  mean |szsum/4 - t2| = 55.4   against a mean t2 of 6192.1
```

That last line is the whole defect. `field_shaded_queue_recipe.cpp` computes

```cpp
depth -= (int64_t)std::max(record.affine.t[2] - 256, 0) * 4;
```

and `t2` is, to **0.9%**, exactly `szsum / 4` -- the face's own mean projected depth. So the bias is
`szsum - 1024` and the subtraction leaves a near-constant `depth ~ 1024` no matter how far away the
object is. What remains is not range at all, it is the spread of the four vertices about the object
origin, which is why `depth` correlates with range at -0.27 instead of +1.

`ProjParams::pzToOrd` is reversed-Z and the compare is `GREATER_OR_EQUAL`, so a larger depth wins.
An ordering whose range term has been cancelled and slightly inverted therefore lets a distant gem
beat near terrain -- the reported symptom, arrived at from the arithmetic rather than from the
pixels.

Retail does not do this. Its bins track range: bin 171 at distance 26333 against bin 105 at 17518,
a bin ratio of 1.63 against a distance ratio of 1.50. The port collapses the same scene into bins
16-60.

### The bias term itself is correctly recovered; the value fed to it is not

Read `r_moby.s` 0x80022DA8-0x80022DB8:

```
80022DA8   addi $t8, $v1, -0x100      # t8 = TRZ - 256
80022DB0   bgez $t8, .L80022DBC
80022DB4    sll $t8, $t8, 2           # delay slot: ALWAYS runs, so t8 = (TRZ-256) * 4
80022DB8   addi $t8, $zero, 0x0       # ... clamped to 0 when negative
80022DC4   ctc2 $v1, C2_TRZ           # and $v1 is proven to be TRZ two instructions later
```

`max(TRZ - 256, 0) * 4` is exactly what the recipe implements, so the FORMULA is right. What is
wrong is the operand: the recipe's own comment says "TRZ is approximated by the actor's view-Z
origin", and that approximation makes the bias equal the quantity it is subtracted from. Retail
derives TRZ through 0x80022CCC-0x80022D18 -- a doubling, then an optional GTE `GPF` scaled by the
byte at `0x57($fp)` and shifted right 5 -- before it reaches `ctc2`. The port skips all of that.

### Two smaller arithmetic differences found in the same read, not yet the cause

Both real OT arms (quad 0x800233E0-0x800233FC, triangle 0x800234B4-0x800234DC) end:

```
  v0 = sz0+sz1+sz2+sz3 - t8 ;  if (v0 <= 0) reject ;  v0 >>= 5 ;  v0 += (s0 & 3)
```

* Retail adds `(s0 & 3)`, a 0..3 sub-bin from the primitive word, AFTER the shift. The recipe does
  not add it anywhere.
* The recipe adds `512` to `depth` before the shift when `reverseFacing` (+16 bins). Neither real
  arm has that. The only `+0x200` in the function is at 0x80023800, in a DIFFERENT arm that sums
  only two sz and is guarded by `bgez $t6`. It was borrowed from the wrong path.

Neither accounts for a 154-bin gap, so they are corrections to make, not the fault.

### Falsifier

Recovering retail's TRZ derivation must: raise authored-bin agreement above 625/664, move node
`8016F0C8` from bins 17-23 toward retail's 171, restore `depth vs t2` to a strong positive
correlation, and spread the used bins well beyond 16-60. If the correlation stays near zero the
diagnosis here is wrong.

### Next measurement

The remaining unknown is the unit relationship between retail's TRZ and its sz table at
`0x200($vN)`. Static reading of the frustum tests (0x80022C08-0x80022C94, constants 0x4D/0x66/0x28
and a 0x1100 compare) puts pre-doubling `$v1` in the same unit and magnitude as the port's `t2`
(450..12148), but under every consistent reading of that the retail bias would collapse its own
range too, which it demonstrably does not. So the assumption to break is the sz unit, and settling
it needs the console oracle to read retail's actual `0x200` table rather than more static reading.

### The obvious explanation for WHY retail differs does not survive its own check

The tempting reading is a unit mismatch: that retail's sz table holds RTPS `SZ3` while the port's
`projected[].sz` holds a raw view-Z, so the port's bias and its sum coincidentally annihilate while
retail's do not. That reading fails. The GTE computes

    MAC3 = (TRZ*4096 + R31*VX + R32*VY + R33*VZ) >> 12   ->   SZ3 ~ TRZ + rotated_local_z/4096

so retail's `SZ3` is ALSO approximately `TRZ`, its four-vertex sum is also approximately `4*TRZ`,
and `szsum - max(TRZ-256,0)*4` would leave retail with about 1024 too -- bin ~32, not the bin 171
the oracle reads off its ordering table. Under the same arithmetic retail should collapse exactly
as the port does, and it does not.

So one of these three is false and the measurement has not yet said which:

1. retail's sz table at `$gp + 0x200 + index*4` is not `SZ3`;
2. the `bin` the oracle reports is not the index this arithmetic produces -- note 0x80023290-
   0x800232A0 sets `s1 = D_8006FCF4`, `s2 = s1 + 0x900` (0x900/8 = 288 eight-byte entries) and the
   `.L80023440` arm indexes by `s2 - v0`, i.e. from the OTHER end, so an OT position and this `v0`
   need not increase together;
3. the port's `t2` is not retail's TRZ after all, despite `FixedAffine::t` being the GTE translation
   vector.

(2) is the cheapest to settle and the most likely, because it would also explain the direction: a
reversed OT walk turns a small `v0` into a large reported bin. Settle it by reading `D_8006FCF4`
and the linked position for one known primitive rather than by reading more assembly.

None of this weakens the measurement above. That the port's own bias cancels the port's own range,
at r = -0.27 over 7,127 faces, is a fact about the port and is wrong whatever retail turns out to
do.

## 2026-09-19: ROOT CAUSE — the port authors the INTRA-OBJECT face order as if it were the world bin

The contradiction above is resolved, and candidate (2) was right in a stronger form than guessed:
the two `bin` numbers are indices into **two different ordering tables**.

Measured, not inferred. The oracle's `kOtPointer` is `0x80075820`, and `external/spyro-1`'s
`game.sbss.s:333` gives that address the name **`g_WorldOT`**. A run prints
`ot_base=0x801BFBB8 ot_is_8006FCF4=false` on all 190 frames: the oracle walks the world OT, while
`func_80022A2C` links its packets into a private 288-entry sub-table at `D_8006FCF4` (r_moby.s
0x80023290 sets the base, 0x80023418 does the `sll v0,3; add v0,s1`).

The two are joined at the end of the producer (0x80023958-0x800239EC):

```
.L80023958   sub $v0, $s3, $s2        # s2/s3 are the lowest/highest sub-bin used
.L80023990   lw  $v1, -0x4($s3)       # walk the 288-entry sub-table from the FAR END down,
             addi $s3, $s3, -0x8      #   chaining its packets into ONE list
.L800239B8   lui $v1, %hi(g_WorldOT)
             cfc2 $a0, C2_DQB         # the world bin, carried in the GTE's DQB register
             sll  $a0, $a0, 3         #   * 8 bytes per entry
.L800239EC   add  $v1, $v1, $a0       # and the whole chain is spliced in at THAT ONE bin
```

So the 288-bin table orders faces **within one moby**, and the moby as a whole gets a single world
position from `DQB`, which the same function computed per object:

```
80022CD8   sra  $t9, $v1, 6           # v1 is TRZ, already doubled at 80022CD4
80022D9C   sra  $a0, $s4, 24          # the object's own bias byte, from lw 0x44($fp)
80022DA0   sub  $t9, $t9, $a0
80022DA4   bgez $t9, .L80022DB0
80022DAC   addi $t9, $zero, 0x0       # clamped at 0
80023288   ctc2 $t9, C2_DQB           # parked in DQB until the splice reads it back
```

    worldBin = max((TRZ*2 >> 6) - (s4 >> 24), 0)

**The port computes nothing corresponding to this.** `field_shaded_queue_recipe.cpp` produces only
the intra-object face index and `field_shaded_queue_emit.cpp` submits it as
`scene_painter_order::queuedWorld(face.otBin, face.paintGroup)` -- an intra-object ordinal presented
as a world-ordering position. Gems therefore sort against terrain by their face order within the
gem, which is unrelated to how far away the gem is. That is the user's symptom, fully accounted for.

It also explains the split that pointed here, which nothing else did: **0x80022A2C differs on 39 of
39 while five other painters differ on none**. Those five author world-OT positions directly and are
comparable with retail's `bin` as the diff assumed; only this producer goes through a private
sub-table, so only its numbers were being compared across two index spaces.

### What this retracts

The "94.13% of authored bins match retail" figure is sound for the five direct painters and
**meaningless for 0x80022A2C** -- for that producer it compared a sub-table index against a world
OT position. The "native bin 17 vs retail 171 (-154)" line is not a 154-bin error in one quantity;
it is two different quantities. The 7,127-face measurement is unaffected: it is internal to the port.

The bias finding also re-reads. `max(TRZ-256,0)*4` genuinely belongs to the intra-object sub-bin
(0x800233EC), and `t2 ~ szsum/4` collapsing it is still wrong -- but its consequence is now a
squashed face order WITHIN a moby, not a wrong world position. The world position is missing
outright, which is the larger fault and the one to fix first.

### Next

Author the world bin from `max((TRZ*2 >> 6) - biasByte, 0)` and keep the sub-table index as the
within-object suborder, which is what `PainterReplayOrder{ot_bin, link_ordinal, chain_suborder}`
is already shaped for. `biasByte` is the top byte of the word at `0x44($fp)`; identify its native
owner before wiring it, and do not substitute a constant.

Falsifier unchanged in spirit and now correctly targeted: the authored WORLD bin for 0x80022A2C
must track range (strong positive correlation with `t2`, where it is now -0.27), and node
`8016F0C8` must reach retail's neighbourhood of 171 rather than 17-23.

## 2026-09-19: the world bin is FIXED and verified; the visible symptom is NOT

`Moby::m_DepthOffset` is the bias byte, confirmed by offset rather than by name: `m_Class` sits at
0x36 (matching the oracle's known-good `kMobyClass = 54`) and `m_State` at 0x48 (`kMobyState = 72`),
which puts `m_DepthOffset` -- declared immediately before `m_State` -- at exactly **0x47**, byte 3
of the word `lw $s4, 0x44($fp)` loads, read with `sra` because the decomp declares it `char`. Its
own doc comment is "Offsets the sorting depth of the entire Moby".

So the recipe now computes, once per record:

    worldBin = max((affine.t[2] >> 6) - m_DepthOffset, 0)

and the submitter passes `queuedWorld(face.worldBin, face.paintGroup, face.otBin)` -- the world
position, and the 288-entry sub-table index as the within-moby suborder. The suborder is inverted
(`kQueuedWorldSubBins - 1 - subBin`) because retail chains its sub-table from the highest used entry
downwards (0x80023990) so the highest sub-bin reaches the chain head and draws first, while
`painterReplayBefore` orders `chain_suborder` smallest-first.

**Measured, and the falsifier passes:**

```
  world_bin vs range (t2)  : r = +1.0000     (the sub-bin it replaced was -0.2745)
  world_bin range          : 3 .. 185        (the sub-bin used only 16 .. 60 of 288)
  authored bin vs retail   : 415 / 415  = 100.00%    (was 625/664, with all 39 misses here)
  0x80022A2C in that set   : 54 submitted / 34 matched, 0 differing  (was 39/39 differing)
```

The painter is present in the comparison with 34 matched primitives, so the 100% is agreement and
not an empty denominator. No record hit the new `worldBin >= kWorldOtBins` refusal. Gate 47/47.

### And it does not change what the user sees

Re-censused at f2400 of `artisans-arrival`, same predicate, fresh capture:

```
                              both  native-only  console-only
  before the fix                20          116           41
  after  the fix                20          116           41
```

The frame did change (the native PNG is 66230 bytes against 66322 before), so this is not a stale
capture -- the violet pixels are simply the same ones. **The replay order is not what decides them.**

`field_shaded_queue_submitter.cpp:65` sends `depth[i] = pzToOrd(face.vertices[i].viewZ)`, a
per-vertex projected depth with no connection to the ordering-table arithmetic, and that is what the
GPU depth test uses. The diff shows the problem directly: two objects at ranges 15197 and 8336 get
native depths **0.022290 and 0.022285**, five parts per million apart, while retail puts them in
bins 111 and 49. The depth-vs-retail disagreement rate is 6.43% and did not improve, as expected --
nothing in this change touches `depth[]`.

So issue 0120 has two faults, not one:

1. **the world ordering position** -- missing entirely, now recovered and verified above; and
2. **the submitted per-vertex depth** -- degenerate across range, which is what actually decides the
   pixels the user is looking at. This is issue 0105's measurement seen from the other side.

Fault 1 is real and worth having: the painter replay is what orders faces that tie on depth, and it
was ordering them by an intra-object ordinal. But it was not the cause of the screenshot, and
claiming the gem symptom fixed on the strength of 415/415 would be exactly the "green test cannot
overrule the running product" failure this repo warns about.

**Next, and this is now the whole issue:** find why `NativeProjectedVertex::pz` barely varies with
range. `t2` (`affine.t[2]`, the view-space object origin) correlates +0.9999 with the projected sz
sum over 7127 faces, so the per-object view depth IS right and available; the per-vertex `pz` that
reaches `pzToOrd` is what collapses. Falsifier: native depth for the pair above must separate in
proportion to 15197 vs 8336, and the 6.43% disagreement rate must fall.

## 2026-09-19 (later): the depth and the authored bin are two different scales, per producer

Measured on psxport `077f5d0c`, spyro `3b4fbe6`, over the full `artisans-arrival` replay with
`PSXPORT_ACTOR_SCENE_ORACLE=1 PSXPORT_DEBUG=actororacle`, 833,749 native item lines in the tail.

**The recipe is exonerated, by the tool's own words.** `tools/actor_oracle_diff.py`:

```
  authored bin vs retail bin (independent of depth):
    compared 655 of 655 matched primitive(s) that carry one
    identical to retail : 655  (100.00%)
    every authored bin matches retail, so the recipe is NOT the fault here;
    look at the ordering rule and the depth buffer instead.
```

**And here is the ordering rule failing, on one named pair.** Retail puts the gem BEHIND; the port's
submitted depths put it IN FRONT, which is the screenshot:

| object | class | retail bin | native depth | port's verdict |
|---|---|---|---|---|
| `8016F0C8` | 83 (gem) | **171** (farther) | 0.012709 | **nearer** |
| `8016FA10` | 10 | **105** (nearer) | 0.009630 | farther |

Larger depth wins under `GREATER_OR_EQUAL`, so the gem beats the object retail ordered in front of
it.

**The scale disagreement, with a denominator.** If a producer's depth and its authored bin came from
the same view Z, then `ord x bin` is one constant for every producer (`ord ~ nearp/pz` and
`bin ~ pz/64` give `nearp/64 = 2.66`). Measured over 833,749 items, median with the p10..p90 band:

| producer | items | median ord x bin | p10 | p90 |
|---|---|---|---|---|
| `0x80023AC4` paired actor | 216,249 | **0.90** | 0.85 | 0.94 |
| `0x80059A48` Spyro shadow | 19,264 | 1.06 | 0.87 | 1.12 |
| `0x80020F34` secondary actor | 90,567 | 1.09 | 1.07 | 1.12 |
| `0x800580F4` glow | 9,632 | 1.26 | 1.22 | 1.29 |
| `0x8001F798` actor draw | 432,477 | 1.30 | 0.77 | 2.28 |
| `0x80022A2C` shaded moby / gems | 47,132 | **2.31** | 2.16 | 2.36 |
| `0x800584C4` sparkle | 3,920 | 4.66 | 4.56 | 5.29 |
| `0x80059F8C` unidentified | 14,508 | **4.89** | 4.80 | 4.97 |

Each producer's own band is tight -- these are per-producer constants, not noise -- and they span
**5.4x** between the extremes. Only `0x80022A2C` is near the 2.66 a self-consistent producer should
give. One shared D32 buffer cannot arbitrate between eight different scales, so the authored order
loses wherever two producers meet.

### FALSIFIED: a per-producer near plane

`proj_params.h` documents exactly this failure ("a frame in which producers disagree puts their faces
on two different depth scales in ONE shared D32 buffer -- the shape of a wrong-occlusion report") and
ships `PSXPORT_DEBUG=projplane` for it. Run over the same replay, 40,000 observations:

```
  36960  projH 341 -> 341 (unchanged)
   3040  projH 0 -> 341 CHANGED
```

Every change is the first install. `projH` is 341 for every producer all frame, so `pzToOrd` is one
function with one near plane and the spread above is NOT a near-plane disagreement. The hypothesis is
dead; do not re-derive it.

### Correction to the earlier entry

The reading above it -- "per-vertex depth degenerate across range", "0.022290 vs 0.022285" -- was
partly my own measurement error. `shadedface`'s `szsum` is always a FOUR-term sum
(`projected[index[0..3]].sz`) even when `count=3`, so dividing it by `count` inflates a triangle's
mean by 4/3. Dividing by 4 instead, over 200,000 faces:

```
  count=3: 137,670 faces   (szsum/4)/t2  median 0.9931  p10 0.9817  p90 0.9999
  count=4:  62,330 faces   (szsum/4)/t2  median 0.9931  p10 0.9829  p90 1.0026
```

So `0x80022A2C`'s per-vertex depth DOES track its object's view Z, and its depth and bin are mutually
consistent -- which is why it is the one producer near 2.66. `m_DepthOffset` is also not the
mechanism: it was 4 on all 200,000 faces (5 distinct mobys), moving the bin by a median 4.0%.

### Next: the depth must stop being a second opinion

Within an authored painter domain the replay key IS the order -- it is retail's own OT, now verified
655/655 -- so the depth buffer must only separate BINS and never contradict them. The framework
already does exactly this for `sort_key` items: `rq_apply_ot_lifo_depths` gives a whole bucket one
band depth, and its comment says why ("the depth buffer stops being a second opinion that has to be
argued with"). `render_queue.cpp:1903` excludes `it.painter_object`, and every Spyro prim carries
one, so no Spyro face has ever reached it.

Falsifier for the fix: the `ord x bin` table above must collapse to ONE constant across all eight
producers, the `8016F0C8` / `8016FA10` pair must invert to match retail, and the f2400 violet census
(20 both / 116 native-only / 41 console-only) must fall.

### Instrument defect found while doing this

`ProjParams::reportProjH` is documented in `proj_params.h` as saying "which plane was installed by
whom", so that "they all agree" and "nobody measured" are different answers. The implementation
prints only `projH {} -> {}` with no owner, so a run in which one producer never installs a plane is
indistinguishable from one where all agree. The verdict above survives that gap only because the
distribution is two-valued and every CHANGED line is a 0 -> 341 first install.

## 2026-09-19 (later still): the band is built and enforced, and it moves NO pixel I can reach

Both halves exist. Framework: `PainterReplayOrder::band_ord` plus `painter_band_depth.*`, which
refuses a frame where one bin carries two depths, a nearer bin is given a farther depth, or the
submitted depth is not the declared one (psxport `4c720e38`, `b1242c07`, gate 164/164). Title:
`scene_painter_order::bandDepth` returns `pzToOrd(ot_bin << 6)` and eleven submitters send it.

It fires. On the picture oracle's own artisans route, every frame:

```
[painterband] f1 1 authored domain(s): 830 of 1320 command(s) banded across 407 bin(s), 490 kept per-vertex depth
```

**And the picture does not change, at all.** Paired measurement -- the banded build, then the same
tree with the title half stashed and rebuilt, same route, same `--play 1600 --frame-step 200`:

| | result |
|---|---|
| native PNGs compared | 8 |
| byte-identical | 8 of 8 |
| violet census, both arms | f200 42 · f400 42 · f600 41 · f800 58 · f1000 58 · f1200 43 · f1400 55 · f1600 86 native-only |

Not one pixel differs. The same holds standing next to a gem: `drive.py gameplay --seek-class 83`
reached class 83 at distance 208 on both builds and produced the same file, md5
`7d895eff0c1e5f231b8a6ca8d448f191`.

**That is not the falsifier failing, and it must not be recorded as one.** Neither scene contains the
defect. The gem-seek shot puts Spyro in FRONT of the gem, where nothing occludes it, so no depth
contest happens at all. The route frames f200..f1600 have a non-zero but UNCHANGED violet count,
which says their violet disagreement has some other cause. The pair that does exhibit it,
`8016F0C8` at bin 171 against `8016FA10` at bin 105, was measured at f2400.

**f2400 is no longer reachable, and it is issue 0114, not a new regression.** The native run aborts:

```
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 2 (no producer is registered for this stage)
[render:error]   fatal boundary: guest pc=0xDEAD0000 stage=2/3/2 load_stage=4294967295
```

The abort itself is the known missing `0x8001A40C` producer. WHEN the route meets it has moved --
play-frame 4800 at 15:14, 2400 at 17:04, now between 1600 and 2400 -- and I first recorded `987eaa1`
as the suspect. THAT WAS WRONG, and a reader should not chase it: `987eaa1` writes no guest memory
and cannot move the simulation.

`tools/oracle_compare.py` gives the real answer. Every gated field MATCHES at all 12 checkpoints,
including `player.position` and `player.state`. But the informational `player` region first diverges
at `gameplay[3]` -- the FIRST segment that holds a direction -- at +30, native 01 against console 03,
and stays diverged for every checkpoint after. That is issue 0114 exactly: `g_LevelTicks` keeps a
VSync-interrupt-phase offset the host clock cannot reproduce (issue 0110), the camera inherits it as
about 1.3 degrees of yaw, and Spyro's d-pad is CAMERA-RELATIVE -- so a recorded pad route does not
replay, and anything that shifts frame timing moves where the player ends up.

The consequence for this issue is the part worth keeping: **a recorded-route frame tag is not a
stable scene**, so `f2400` does not name the same view twice and no before/after at a route tag can
be trusted past the first steered segment. `drive.py --seek-class 83` is not affected because it
re-steers from the guest camera every step, which is why it reached class 83 at 208 on both builds.

So the state of this issue is: the recipe is exonerated (655/655), the eight-scale depth
disagreement is measured, the fix for it is built and mechanically enforced, and it is **unverified
against the defect** because no scene that exhibits the defect is currently reachable.

Falsifier, unchanged and still open: the `ord x bin` table must collapse to one constant, the
`8016F0C8` / `8016FA10` pair must invert, and the f2400 census (20 / 116 / 41) must fall.

Next: reach a gem that is BEHIND terrain, using a SELF-STEERING route rather than a recorded pad,
since the recorded one no longer names a stable scene. `drive.py --seek-class 83` walks to a gem but
stops in front of it, where nothing occludes and no depth contest happens -- that is why it produced
an identical picture on both builds and is the wrong probe. What is needed is a driver that places
the camera so a known gem is occluded, then shoots. Until such a scene exists, no claim about this
defect being fixed is supportable.

### Instrument defect: a deliberate refusal arrives as signal 11

`SpyroRenderer::abortUnimplemented` is a by-name refusal, and it reaches the driver as
`native REPL exited (code 139)` with a `[watchdog] FAULT (signal) signal = 11` backtrace. A refusal
that is indistinguishable from a segfault costs a reader the first minutes of every such failure; it
cost them here.

## 2026-09-19 (final): the depth buffer is NOT a second authority here — the premise was wrong

Everything above from "the depth must stop being a second opinion" onwards rests on one claim: that
the port replays the authored order AND depth-tests, so the depth buffer wins silently where they
disagree. **That claim is false for a regrouped painter range, and it is now measured.**

The probe is `drive.py gameplay --seek-class 83 --seek-arrived 1000`, which re-steers from the guest
camera every step and so reproduces a scene exactly (two runs of one binary gave byte-identical
frames at 4000, 3000, 2000 and 1000 units). `field_shaded_queue_submitter` is the gem producer named
in the report. Forcing its submitted depth, and nothing else:

| what the gem producer submitted | picture |
|---|---|
| the banded per-bin depth | reference |
| `0.99` on every vertex -- nearest possible | byte-identical |
| `0.0` on every vertex -- farthest possible | byte-identical |
| unchanged depth, colour forced to red | **DIFFERS** |

The colour row is what makes the other three mean anything: the producer demonstrably draws
thousands of pixels in this scene, so "no change" is a real negative and not a probe that never ran.
Nearest and farthest are the widest spread the depth band admits. They agree. **The submitted depth
does not reach the picture for these draws.**

The mechanism is in `render_queue.cpp:emitItem`: under `mPainterRegrouping` it calls
`gpu_vk_set_order_override(core, mPainterPresentationRank)`, and `GpuVkState::set_order` derives the
primitive's depth from that rank. The replay order is the whole answer; `RqItem::depth` is dead for
a painter range.

### What that costs, and what it buys

Reverted as built on a disproven premise:

* spyro `2b6c5da` -- the eleven banded submitters and `scene_painter_order::bandDepth`. It was inert,
  which is exactly what this predicts.
* psxport's `painter_band_depth.*` contract and `PainterReplayOrder::band_ord`. It validated a
  declaration with no effect -- a check that can only ever print one answer, which is the shape this
  project's own rules say to refuse. The `painterplan` extraction and the line-cap ratchet from that
  commit are kept; they were independently good.

What it buys is the real direction. The bins are 655/655 identical to retail and the depth cannot be
the fault, so **the remaining suspect is the replay ORDER itself** -- `link_ordinal` (the twelve
producer phases) and `chain_suborder`, not `ot_bin` and not depth. Retail put the gem at bin 171 and
the object at bin 105; a pure painter replay draws the larger bin first, so the object should already
cover the gem. It does not, so either those faces are not being interleaved in one range, or a phase
ordinal places the gem producer after the producer that should cover it.

Falsifier for the next attempt: with the port's own `painterplan` channel, the gem's face and the
face that should occlude it must appear in ONE range, in retail's relative order. If they land in
different ranges, no ordering rule inside a range can fix it and the domain composition is the fault.

### Instrument note: a uniform scale is not a discriminator

My first attempt scaled every band by `1e-6` and read the identical picture as proof. It proves
nothing: a uniform scale preserves the ordering, so every depth test that passed still passes. Only
breaking the order -- or pinning one producer to an extreme -- discriminates. Recorded because the
mistake is easy to repeat and it looked like an answer.

## 2026-09-19 (closing the loop): the ordering defect is closed, and the metric that said otherwise ranks nothing

With depth eliminated, the falsifier's remaining branch was the replay ORDER. Both halves of it now
answer, on a reproducible gem scene (`--seek-class 83 --seek-arrived 2000`):

**The domain composes into one range.** `PSXPORT_DEBUG=painterplan`, 3109 frames, every one:

```
ranges=1  items=1364  objects=9  domains=1
counts= 8004EBA8:362 800258F0:493 8004FEA0:3 8001F798:262 80022A2C:35 80059A48:16 80023AC4:169 800580F4:8 800573C8:16
first=8004EBA8@2047/...  last=800573C8@3/...
```

`ranges=1` on 3109 of 3109 frames. Terrain (`8004EBA8`), the world scene (`800258F0`) and the gem
producer (`80022A2C`) are in ONE range, and the range runs from bin 2047 down to bin 3 -- retail's
own far-to-near walk. So the faces ARE interleaved and no domain-composition fault remains.

**The bins in that range are retail's.** `actor_oracle_diff.py` on the same scene: 513 native against
513 retail primitives, 512 matched, and `512 of 512 (100.00%) identical to retail`.

Bins are 100% retail, the bins are what the replay draws by, and the replay order is the sole
authority. **The drawn order therefore matches retail**, and the ordering defect this issue was
opened for is closed by `987eaa1` -- the commit that recovered the shaded-moby world bin. Everything
after that commit in this issue, including the eight-scale depth analysis and the banding, was
chasing a quantity that cannot reach the picture.

### The instrument that caused it, now fixed

`actor_oracle_diff.py` leads with:

```
  disagreeing with retail  : 12422
  disagreement rate        : 11.85%
```

computed from the submitted per-vertex depth. That number ranks nothing the player sees, and reading
it as a remaining defect is what produced the "eight scales, one shared D32 buffer" section above and
a day of work on a fix that moved no pixel. The tool now prints, immediately under the rate, that an
authored painter domain's submitted depth does not reach the picture and that the authored-bin
comparison is the one that decides -- and, when the bins are 100%, that the drawn order matches
retail rather than the old "look at the ordering rule and the depth buffer instead".

### What is still open

A direct visual confirmation against the console at a scene where a gem is actually occluded. The
evidence above is a construction proof (retail's bins, retail's order, order is the only authority),
not a picture, and the recorded route that produced the user's frame can no longer reach it. The
remaining step is a console-comparable checkpoint in a scene with an occluded gem.


## 2026-09-20 — the console-comparable checkpoint now exists, and this scene is clean

Issue 0126's blocker is gone. `tools/oracle_spyro1.py` gained `settled_play`, driven to
`GS_Playing and g_GameTick >= 180`; both cores reach it after the SAME 179 game frames, so the
Artisans courtyard is photographed at one moment on both and the comparison is real for the first
time.

The comparator was also changed, because it could not have shown this symptom if it were there.
`compare_pictures` counted any pixel inequality, and 29.45% of this frame differs by exactly one
15-bit colour step — rounding a player cannot see. A gem drawn in front of terrain is a CONCENTRATED
block of large-magnitude difference, and it would have been invisible inside a 54.62% headline.
`PictureDiff` now separates `significant` (beyond one colour step) from the bare count and ranks
`worst_tiles` by significant pixels only, so a mislaid object is what the tool points at.

What the courtyard shows (`scratch/picture/settled_play.magnitude.png`): polygon EDGES outlined
everywhere (sub-pixel rasterisation placement), dither speckle over textured ground and sky, and two
concentrated blobs near (272,112) and (320,96) that are Sparx and a sparkle — moby positions, which
this title excludes from the declared ranges. **No solid contiguous region is drawn wrongly**, which
is the signature this issue is looking for.

### Why this does NOT close the issue

The courtyard at `settled_play` has no gem behind an occluder in it, so a clean frame here is not an
answer to the report. What changed is that the instrument can now give one: the remaining step is
unchanged in kind but no longer blocked — drive `settled_play`'s route on to a scene where a gem sits
behind terrain, and read the significant-difference map there. A CONCENTRATED large-magnitude blob at
the gem is the symptom; its absence, in a scene that actually contains the case, is the clearance.

How far that route currently reaches, measured the same day with `--play 480 --frame-step 60`:
`settled_play` and 120 further frames of the scripted route compare (24.90% and 24.97% significant,
the same spread residual as the courtyard); f180 is refused as a whiteout and f240 on the
camera/cutscene-tick residual of issues 0110/0114. So the comparable gameplay window is about 120
frames of forward-and-turn motion, which does not leave the courtyard. Reaching a gem behind an
occluder needs either that residual closed (0114) or a scene with the case closer to the spawn.
