---
id: 111
title: Spyro's shaded-moby queue recipe disagreed with the authenticated renderers on light table, variant bits, colour scale, semi-transparency and depth unit
status: investigating
symptom: shaded-moby faces (0x80022A2C) used the wrong light entry, an unapplied delay-slot colour shift, the wrong semi-transparency rule and the record-local depth unit; both focused tests existed but were never registered in CTest, so nothing failed
tags: spyro1,render,field,actor,shaded,lighting,semi,depth
created: 2026-09-13
updated: 2026-09-13
---

Affected state items: S005, S019.

## What disagreed

The native shaded-moby producer (`0x80022A2C`, `func_80022A2C` = "Render shaded Mobys with lighting"
in `external/spyro-1/src/gamestates/draw.c`) implemented its colour, face-classification and depth
rules without reading the authenticated renderer, and its two focused tests were never named by the
CMake CTest loop, so nothing failed. Decoding `external/spyro-1/asm/renderers/r_moby.s` against the
recipe found these disagreements:

| native before | authenticated renderer | where |
|---|---|---|
| light table `0x8007E44C` | `0x8006E44C`, `D_8006E44C[17]` "Specular shaded color list" | `asm/data/math.data.s:1868`, `r_moby.s 0x80023724`, `include/moby.h:517` |
| variant from `primitive.normal & 3` | the record's first stream word: `andi $t6,$at,0x1` / `andi $t7,$at,0x2` (`$at` = the 8-byte record's word 0) | `r_moby.s 0x80023320..0x80023534` |
| reverse-facing factor `colourMac >> 2` | `>> 8` in the `bgez` delay slot (`0x800237F4`) **plus** `>> 2` on the facing arm (`0x800237F8`), i.e. `>> 10` | `shade()` |
| colour command `opcode - lightingOffset + rgb`, refusing when the top byte changed | no opcode check is warranted: the source overwrites `$t7` with `0x02000000`/`0` at `0x8002374C..0x80023754` before `sub $a3,$a3,$t7` at `0x800238D0`, so the light-table byte offset never reaches the command word | `r_moby.s 0x80023720..0x800238D4` |
| depth `raw_view[2]` | `pz`, the unit every other native submitter already uses (see issue 0105) | `projectVertex()` |
| `semiTransparent = (variant == 3)` | the source keeps `0x22`/`0x2A` only while `TRZ < 0x800` **and** the face is front-facing | `r_moby.s 0x80023720..0x800238CC` |

The semi-transparency rule had been guessed twice in opposite directions — first `variant == 3`
(always semi-transparent), then a flat `false` (always opaque) — and neither matches the source. The
source derives it from the same near-camera and facing test that already gates the face, so the
recipe now derives `variant != 0 && nearCamera && firstFacing >= 0`; the vertex-colour arm stays
opaque (Gouraud `0x30`/`0x38`).

The wide-engine projection centre is also taken from the host aspect policy
(`gpu_vk_wide_engine_ofx`) instead of the guest `geomOfx`, so the shaded arm projects around the
widescreen centre like the other FIELD producers.

## Verification

`test_field_shaded_queue_recipe`, `test_field_shaded_queue_scene`, `test_spyro_flame_recipe` and
`test_spyro_flame_matrix` pass under Clang, and both shaded-queue targets are now registered in the
CMake CTest loop. The recipe tests pin the variant bits, the depth unit, the near/far
semi-transparency branch and the flat arm's colour as the selected light entry.

## Residuals

`TRZ` is the GTE Z FIFO value of the last projected vertex; the recipe approximates it with the
actor's view-Z origin (`record.affine.t[2] < 2048`) because it projects through
`native_projection::project` rather than the GTE. A large or off-centre mesh can cross that threshold
differently. No live shaded-arm frame has been compared against the full console yet, so the change
is source-grounded but not parity-verified; the next discriminator is the shaded arm's emitted
colour/depth against a console capture at a matched phase.

## Also fixed in the same change

`spyro::flame_recipe`'s new near-plane rejection read `previous[i].viewZ`, which is
`native_projection`'s `pz` and is clamped to at least `h/2`, so that half of the guard could never
fire. It now carries the previous cross-section's behind-camera state explicitly, and both new
rejections are counted (`TipRingBehindCamera`, `RibbonBehindCamera`) and reported by
`fx_spyro_flame`'s census instead of being silent skips.

## The live comparison now exists, and the shaded arm is the one producer retail does not echo

`PSXPORT_ACTOR_SCENE_ORACLE=1` runs retail's own moby walker (`0x80019698`, which calls
`func_8001F158`, `0x8001F798`, `0x800208FC`, `0x80020F34`, **`0x80022A2C`**, `0x80059F8C`,
`0x80023AC4`, `0x80059A48`, `0x80058D64`, `0x80058BA8` — `src/gamestates/draw.c:606`) over the state
the native producers just read, and `tools/actor_oracle_diff.py` matches the two streams on the
multiset of `(x, y, rgb)` vertices. The oracle now logs each native record's painter object and the
`g_SonyImage.m_ShadedMobys` (`0x800720F4`) length at the moment the retail body is dispatched, so an
unmatched primitive names the producer that submitted it instead of only the total.

One Artisans capture (`scratch/logs/actororacle_shaded_list2.log`, 6 frames, `--settle 12`), per
frame, native records by producer as *submitted / matched / recoloured / unmatched*:

| producer | frame 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| `0x8001F798` regular mobys | 317/309/0/8 | 315/307/0/8 | 317/309/0/8 | 321/313/0/8 | 322/311/0/11 | 321/313/0/8 |
| `0x80020F34` specular mobys | 74/74/0/0 | 76/76/0/0 | 74/74/0/0 | 76/76/0/0 | 76/76/0/0 | 76/76/0/0 |
| **`0x80022A2C` shaded/sprite queue** | **23/0/0/23** | **25/0/0/25** | **23/0/0/23** | **25/0/0/25** | **23/0/0/23** | **24/0/0/24** |
| `0x80023AC4` Spyro | 164/164/0/0 | 171/171/0/0 | 164/164/0/0 | 171/171/0/0 | 164/164/0/0 | 171/171/0/0 |
| `0x80059A48` Spyro's shadow | 16/16/0/0 | 14/16/0/0 | 16/16/0/0 | 16/16/0/0 | 16/16/0/0 | 16/16/0/0 |

The shaded producer is **never matched, on any captured frame** (17 frames across two captures,
22-26 primitives each), while the four producers around it match to within 0-8 primitives. A
separate run over 17 frames showed the same shape per frame (22-26 submitted, 0 matched, and most of
each frame's `native only` total). The measured horizontal shift is a constant `-86` on every frame.

Two further measurements bound what that means:

- **The retail arm was handed a non-empty list.** The summary line reports
  `shaded_list=104/104`: `g_SonyImage.m_ShadedMobys` held 104 records before the retail body ran and
  the same 104 after, so the shaded pass read a populated list and did not clear it. The records are
  not native inventions either — the list is filled by the guest's own `func_800521C0` ("Queue render
  mobys"), which the port dispatches in-process (`spyro_field_build_moby_lists`).
- **Retail's decoded stream contains no flat primitive at all.** Retail codes decoded on those
  frames are only `0x30/0x32/0x34/0x38/0x3C` — every one of them a Gouraud code — while
  `func_80022A2C`'s flat arms are the `0x22`/`0x2A` family this recipe emits. So retail's shaded pass
  contributed *zero* primitives, not misprojected ones.

The three records the native does draw are `class 83` (gem) instances whose record gates pass on
both sides: read straight from guest RAM
(`scratch/oracle-comparison/shaded_queue_probe.py`), record `0x8016D6A8` has
`extent=0x0118 radius=6656 pos=(80732,61082,6989)`, camera-relative `(-1065,-4092,795)`, and the
`0x80022B40..0x80022C70` gates evaluate to pass for all three (`coarse`, the `(|x|-102)*4-(z+77)*3`
gate and the `z+40-(|y|-121)*3` gate), using the asm's `IR = (Y, Z, X)` ordering — which the native
implements deliberately (`actor_transform_math::worldAffine`: `view = transform(camera, {relative[1],
relative[2], relative[0]})`). The face stage is therefore not reached-and-clipped on the record path;
retail's pass declined the records before it or not at all.

So this issue's residual is now **reproduced, not explained**, and the explanation has to come
first: either the native shaded arm includes records retail excludes (a real inclusion/culling
defect, and the 23 faces would be geometry retail never draws), or the oracle's retail arm cannot
see that pass at all (a comparison blind spot, in which case the four agreeing producers prove
nothing about the fifth). The measurement cannot choose yet, and the colour/semi-transparency/depth
rules this issue changed are still **source-grounded but parity-unverified**: a producer with zero
matched primitives has zero recoloured primitives, so "0 recoloured" is not evidence about it.

Next discriminator: instrument retail's `0x80022A2C` inside the oracle arm — count the records its
loop body actually processes (the `sb $zero, 0x51($fp)` at `0x80022B28` is one per record) and the
packets it links — instead of inferring both from the decoded stream. A dispatch of `0x80022A2C`
*alone* is not that instrument: it aborts (SIGABRT) because the pool cursor, `g_MobyShadows` and the
GTE matrices are set up by the passes before it, so the count has to come from inside the full
`0x80019698` arm.

### Note (2026-09-13)
Live Actor-scene-oracle comparison now exists (17 frames, two captures): producer 0x80022A2C is 22-26 primitives per frame with ZERO matches on every frame while 0x8001F798/0x80020F34/0x80023AC4/0x80059A48 match to within 0-8. Retail's decoded stream holds only Gouraud codes 0x30-0x3C (no flat 0x2x at all), and g_SonyImage.m_ShadedMobys held 104 records before and after the retail body ran, so the retail shaded pass was handed a populated list and contributed nothing. The 3 records the native draws pass retail's record gates as decoded from r_moby.s. Colours/semi/depth remain parity-unverified: zero matched primitives means zero recoloured primitives. Next: count records processed inside retail's 0x80022A2C in the oracle arm (a lone 0x80022A2C dispatch aborts).
