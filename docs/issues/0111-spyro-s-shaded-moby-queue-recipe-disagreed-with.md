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
