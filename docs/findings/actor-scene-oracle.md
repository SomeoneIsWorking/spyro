# The actor-scene oracle: what the port draws vs what retail draws

`PSXPORT_ACTOR_SCENE_ORACLE=1` makes one frame print both actor streams and
`tools/actor_oracle_diff.py` compares them. It is a value-level oracle: no emulator, no screenshots,
no pixel matching. It exists because "gems look wrong" cannot be acted on until the failing side is
named.

## How it works

`spyro::actor_scene_oracle::compare` runs retail's moby-chain walker `0x80019698` over the same guest
state the native producers have just read, decodes every GPU packet it links into the world OT
(`game/render/gpu_packet_decode.*`, shared with `world_scene_capture.cpp`), restores the OT and the
packet-pool cursor, and prints both streams with denominators. It is armed only by the environment
knob and must never run on a shipping frame: it restores the OT and cursor, but not whatever else the
retail body wrote.

The call sits after `spyro_field_shadow_submit`, because retail's walker draws the player and its
shadow as ordinary mobys. Placing it before them was the first measurement error: it reported ~160
retail primitives as missing that the port simply had not submitted yet.

The differ matches on the MULTISET of `(x, y, rgb)` vertices, so winding does not matter, and it
MEASURES the horizontal shift rather than assuming it — widescreen moves the native projection
centre. It also measures the sign relating retail's OT bin to the port's normalized depth instead of
assuming it; asserting the wrong orientation reported a faithful frame as 94% broken.

## Measured, Artisans, 20 settle frames

    native primitives : 610      retail primitives : 584
    matched           : 582      retail only : 2      native only : 28
    measured horizontal shift: -86

**The actor layer is exact.** Every retail primitive except two is reproduced at the same position in
the same colours. The 28 native-only primitives are the widescreen extras retail never had to draw,
and the 2 retail-only are untextured semi-transparent shadow fan pieces at the frame edge. There were
zero "same shape, different colour" primitives, so actor COLOUR is not a fault.

## The one real divergence: depth vs OT bin

    comparable ordered pairs : 127927
    measured orientation     : a larger OT bin means a smaller native depth
    disagreeing with retail  : 8090 (6.32%)

The disagreement is not spread out. It is 25 instance pairs, dominated by two adjacent mobys:

| instance | faces | retail OT bins | native depth |
|---|---|---|---|
| `0x8016DDE0` | 75 | 141..152 | 0.014344..0.015833 |
| `0x8016DE38` | 72 | 143..152 | 0.014445..0.015610 |
| `0x8016FC78` | 20 | 140..144 | 0.006384..0.006592 |
| `0x8016FA68` | 9 | 142..143 | 0.006383..0.006536 |
| `0x8016F280` | 20 | 53..61 | 0.019239..0.022274 |

Every other instance sits on one monotone bin/depth curve. `0x8016DDE0` and `0x8016DE38` carry
roughly 2.3x the depth of the other instances retail placed in the SAME bin range, i.e. the port puts
them much nearer than retail's OT does.

That is a difference between retail's per-model OT bias and the port's true view depth, and the port
deliberately renders from world position and real depth rather than replaying the PSX OT. So this is
NOT automatically a bug — but it is exactly the shape a depth complaint would take, and it is the
only divergence this frame contains. Naming the two mobys and deciding whether their retail OT bias
is presentation the port must preserve is the open question.

## Gems: measured, and why the frame could not show one

`PSXPORT_ACTOR_SCENE_ORACLE_CLASS=<n>` holds the dump until a Moby of that class is drawn. Spyro 1's
gems are classes 83..87 (`external/spyro-1` `include/moby.h`).

Every armed frame also prints two censuses, whether or not it dumps, because "no gem appeared" has
two very different causes and only one of them is a port fault:

    drawn classes (9): 10 114 120 194 331 336 342 421 501
    level moby classes (live=175 distinct=33): 1 10* 11 18 49 83 84 110 114* 120* ...  (* = drawn)

**Gem classes 83 and 84 are live in the level and were never drawn** — across the settled frame and
300-frame walks left, right and back. That is not a drop: the oracle's own retail side did not draw
them either (2 retail-only primitives, both shadow-fan pieces), so the port and retail agree the gems
are culled. The nearest gem is 8,335/30,622/-2,314 from the camera; the rest are 15k-57k away. They
are simply not in the starting view.

So no gem colour or depth claim can be made from any capture taken so far, in either direction.
Reaching one needs the port to survive the walk, and walking forward from the start enters
`GS_Dragon` (stage 8), which still aborts — issue 0103. That producer is the blocker for the gem
question, not a separate errand.

## Per-instance identity

Actor faces now carry their guest Moby address through
`SourceRecord -> Record -> actor_prefix::Output -> Face` and open a
`RenderDiag::beginObject` scope around each emit, so `RqItem::dbg_node` is the real instance. Before
this every actor primitive was `dbg_node == 0` and no per-instance question could be asked at all.
