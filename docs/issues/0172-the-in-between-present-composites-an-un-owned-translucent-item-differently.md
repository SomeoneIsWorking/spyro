# The in-between present composites an un-owned translucent item differently from the real one

## The symptom

The operator reports Spyro's flame effects — the breath flame and the other effects — flickering
frame to frame.

## What was measured, and where

Route: `tools/drive.py`-equivalent navigation to gameplay, then a **held Circle**. The button is not a
guess: `func_800499C0` (external/spyro-1/asm/nonmatchings/pete/func_800499C0.s:0x80049A50) reads
`g_Pad & 0x20`, which is `PAD_CIRCLE` in `external/spyro-1/include/gamepad.h:15`, and on that edge
sets `g_SpyroFlame+0x98`. The probe reads that word every product step and the `spyroflame` producer
census logs the recipe:

```
parts=8 tips=8 ribbons=32 faces=41 ... parts=8 tips=6 ribbons=79 faces=87 ... parts=0 tips=0 ribbons=0 faces=0
[producers] guest 0x80058D64 native 1087 guest 0 frames 23 (f3024..f3046) spyroflame
```

So the flame is alive for 23 logic frames and the native producer emits up to 87 faces. Every capture
below was taken with `preseq` armed **before** the Circle edge, because `tools/drive.py:889` arms
`preseq` after the `--hold`/`--tap` block and the flame never survives that ordering.

Regions, all in the 684x240 widescreen frame:

| name | box | what it is |
| --- | --- | --- |
| `ctrl` | 20,150 – 130,215 | grass and path the effects never touch — the world-motion floor |
| `spark` | 378,108 – 442,148 | the glow + field-particle cluster (`fps60seq` extents `x=[383..435) y=[112..144)`) |
| `spark2` | 395,115 – 430,135 | a tight box on that cluster only |
| `flame` | 320,100 – 365,120 | the flame's own faces (`fps60seq` extents `x=[327..357) y=[105..114)`) |

`ctrl` reads **exactly 0.00%** on every leg, which is what makes the other numbers attributable: on
this route Spyro stands still and the camera does not move, so nothing in the world can be moving.

## fps60 ON vs OFF, in the effect's own region

Changed pixels per consecutive presented frame (`scratch/flame/seq_diff.py`):

| leg | `spark` | `spark2` |
| --- | --- | --- |
| `tools/wide_only_control_settings.ini` (aspect=1 fps60=0) | 7.85 – 30.94, mean **11.49**, flat | — |
| `tools/shipping_settings.ini` (aspect=1 fps60=1) | 3.71 – 30.12, mean **7.38**, alternating 4.95 / 10.7 | **11.71 – 27.71, mean 20.14** |

The interleaved control on the fps60-ON leg reads 0.00 on every pair.

## The per-present item census — this is the part that decides it

`preseqobj` logs one line per emitted item keyed to the present index. Un-keyed, guest-time drawables
(`key=00000000`, `scene=0`) are the effect and HUD items:

```
fps60 ON   p0..p7 effect-item counts:  123 123 127 127 131 131 134 134
           the full item lists (key, layer, x, y, scene) are IDENTICAL within each pair
fps60 OFF  p0..p6 effect-item counts:   25  29  29  27  29  27  30
```

So with interpolation off every present is a new state and no state is ever repeated; with it on each
state is presented exactly twice. That is the previous session's structural finding, and it is real:
**no effect publisher has a temporal source, so every effect is presented at 30 Hz on a 60 Hz
display.** It is a judder.

**It is not the flicker.** With fps60 OFF the effect region still changes 11.49% of its pixels every
present. An effect that is already unstable at its own native rate cannot be made stable by
reconstructing it.

## The flicker itself

The two presents of ONE logic frame carry **byte-identical effect item streams** and still differ on
**11.7% – 27.7%** of the pixels in `spark2` (mean 20.1%). Identical draw calls producing different
pixels is not an animation and not an effect producer; it is the present path.

Sample of the disagreement (idle leg, no breath, presents 0 and 1 of one logic frame):

```
pixel        p0 (interp)   p1 (real)
(417,119)    (240,208,72)  (200,160,112)
(416,120)    (248,184,248) (160,136,80)
(420,120)    (248,240,0)   (216,160,120)
(418,121)    (248,160,248) (176,152,88)
```

The interp pass composites the translucent effect **more strongly** than the real pass, and `p1 == p2`
across most of the box — so one of the two passes is the odd one out, on every logic frame.

Not an un-cleared blend target: the region's mean over nine consecutive fps60-OFF presents is
stationary (R 173.9, 175.6, 174.1, 176.0, 174.7, 175.5, 176.6, 177.5, 175.3), so nothing accumulates.

## Where it lives

`external/psxport/runtime/psx/present/fps60.cpp:322-345`, `Fps60::presentPass`. The in-between stream
is a merge of the reconstruction's `mSink` and the captured `frame.items`, chosen by

```cpp
takeSink = (sa.layer != sb.layer) ? (sa.layer < sb.layer) : (sa.seq <= sb.seq);
```

`mSink`'s `seq` counter starts at 0 independently of the captured queue's, so reconstructed items
interleave among the un-owned ones by ordinal rather than by the captured ordering, while the real
present (`t == 1.0`, `reconstructThisPass` false for a complete captured queue) plays
`frame.items` in its captured order. The two orders are not the same, and the semi-transparent world
band composites painter objects in stream order (`gpu_vk.cpp` Pass B, one draw per blend-mode bucket
over `s_color_rgba`, depth LOADed from Pass A). An un-owned translucent item therefore blends against
a different background on the in-between pass than on the real one.

`gpu_vk.cpp:1357` (`ct.load_op = clearColorBlack ? CLEAR : LOAD`) is the other candidate read: Pass A
LOADs the colour target rather than clearing it, so the previous present's encoded semi-transparent
content is the base the next pass's Pass B blends over wherever opaque geometry does not overwrite it.
The stationary mean above argues against accumulation over many presents but says nothing about a
two-present alternation, which is what is being measured.

## What was tried, and why it does not work here

An endpoint-pair temporal source for the flame was built to the letter of the other layers
(`temporal::Pair<flame_recipe::Recipe>`, an identity rule over the face set, admission through the
painter preflight, an `owns()` claim on `flame_submitter::kProducerKey`, a per-face lerp with the
material and shade taken from the frame being drawn) and measured:

```
[flametemporal] refused t=0 faces=15 mismatch=face-countx1
[flametemporal] refused t=0 faces=25 mismatch=face-countx1
[flametemporal] refused t=0 faces=32 mismatch=face-countx1
admitted=true: 0 of 3028 intervals
```

A flame is one extruded ribbon whose face set is rebuilt from scratch every logic frame — 15, 25, 32
faces across three consecutive frames. There is no matching source geometry to blend, so the identity
rule refuses, which is AGENTS.md's rule working rather than a gap. The source was reverted instead of
being left in the tree as a construct that can never fire.

## What is left to do

The fix belongs in the present path, not in an effect producer:

1. make the in-between merge preserve the captured ordering of un-owned items rather than interleaving
   them by an unrelated sequence counter, **or**
2. make the real present take the same path the in-between one does, so both composite through one
   order — `capturedQueueIsComplete()` returning false would do it, at the cost of reconstructing the
   real frame.

Either way it is `external/psxport/`, which this session was directed not to touch. Nothing in
`game/render/field/` reproduces retail's flame geometry here, so no title-side change is warranted.