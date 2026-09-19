---
id: 120
title: User reports gems drawing on top of terrain that should occlude them
status: open
related: 0105 (actor-vs-actor depth order, 5.07%). NOT established as the cause of this issue: that instrument walks retail's moby list and cannot see terrain. Both stay open until one measurement connects them.
symptom: "Spyro looks like gems that should be behind terrain rendering on top" (user, 2026-09-19) — REPRODUCED at the framebuffer: at f2400 of artisans-arrival, 116 px show a violet object in the native where the console shows grass (reverse direction only 41 px). Separately, retail's moby walker shows a 5.07% actor-vs-actor depth-order disagreement, which is issue 0105; that instrument cannot see terrain and does not evidence this symptom
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
