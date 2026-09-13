---
id: 111
title: Spyro's shaded-moby queue recipe disagreed with the authenticated renderers on light table, variant bits, colour scale, semi-transparency and depth unit
status: resolved
symptom: shaded-moby faces (0x80022A2C) used the wrong light entry, an unapplied delay-slot colour shift, the wrong semi-transparency rule and the record-local depth unit; both focused tests existed but were never registered in CTest, so nothing failed
tags: spyro1,render,field,actor,shaded,lighting,semi,depth
created: 2026-09-13
updated: 2026-09-14
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
frame, native records by producer as *submitted / matched / recoloured / unmatched*. **The
`0x80022A2C` zero column below is an artifact of the decoder used at the time, not a measurement of
the shaded pass** — see "Resolution" — and is kept because the argument that followed from it is why
the instrument was fixed:

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
- ~~**Retail's decoded stream contains no flat primitive at all.**~~ Retail codes decoded on those
  frames were only `0x30/0x32/0x34/0x38/0x3C` — every one of them a Gouraud code — while
  `func_80022A2C`'s flat arms are the `0x22`/`0x2A` family this recipe emits, so it looked as though
  the shaded pass contributed *zero* primitives rather than misprojected ones. **This measurement was
  wrong**: the decoder refused the flat family, so those packets never reached the decoded set. See
  "Resolution".

The three records the native does draw are `class 83` (gem) instances whose record gates pass on
both sides: read straight from guest RAM
(`scratch/oracle-comparison/shaded_queue_probe.py`), record `0x8016D6A8` has
`extent=0x0118 radius=6656 pos=(80732,61082,6989)`, camera-relative `(-1065,-4092,795)`, and the
`0x80022B40..0x80022C70` gates evaluate to pass for all three (`coarse`, the `(|x|-102)*4-(z+77)*3`
gate and the `z+40-(|y|-121)*3` gate), using the asm's `IR = (Y, Z, X)` ordering — which the native
implements deliberately (`actor_transform_math::worldAffine`: `view = transform(camera, {relative[1],
relative[2], relative[0]})`). The face stage is therefore not reached-and-clipped on the record path;
retail's pass reached its accepted path for those three records — the claim that it declined them
was the decoder artifact described below, and `shaded_flagged=9->9` now shows the acceptance.

## Resolution: the shaded arm was never invisible to retail, only to the decoder

The comparison's decoder, not the port, failed. `gpu_packet_decode::decode` accepted only the Gouraud
polygon family (`0x30`/`0x32`/`0x34`/`0x38`/`0x3C`), and `func_80022A2C` emits the **flat** family
(`0x20`/`0x22`/`0x28`). Every shaded packet in the retail OT chain was therefore refused and dropped,
which is exactly the shape of a pass that drew nothing.

Two instruments were needed to see it, and both are now permanent:

- **The walk reports its own denominators.** `retail side:` now prints
  `scanned= below_filter= refused= decoded=` and, when anything was refused, `retail REFUSED codes
  (n):` with the command bytes. The first run with them up showed `scanned=623 below_filter=0
  refused=33 decoded=572` while the summary had claimed `refusal=none`: thirty-three real packets had
  been silently skipped, and the old line could not distinguish that from a silent pass.
- **The record the pass accepts is readable from guest RAM.** `func_80022A2C` clears `record+0x51` on
  entry to every record it walks (`sb $zero, 0x51($fp)` at `0x80022B28`) and sets it to 1 on the
  record's accepted-lighting path (`sb $a0, 0x51($fp)` at `0x80022D98`, `$a0 = 1`, next to
  `ctc2 $a1, C2_DQA` with `$a1 = 1`). After the retail arm, `shaded_flags=9->9` and the accepted
  records are logged by list ordinal: the same nine world records the native accepts — including the
  three `class 83` gem nodes this issue is about (`0x8016D6A8`/`0x8016D700`/`0x8016D758`, ordinals
  0, 1, 2) — plus six world objects at ordinals 93/97/98/100/101/103. So the record gate never
  disagreed.

### The console agrees, and it draws the gems

`scratch/oracle-comparison/shaded_pass_console.py` observes the real console's `func_80022A2C`
through the packet pool cursor `D_800757B0`, which it commits once per accepted record
(`sw $t9, 0x0($at)` at `0x80023978` world, `0x80023A2C` screen) and which advances with every polygon
it writes. Six rendered Artisans fields, with the cursor snapshotted at every per-record entry
(`0x80022B28`) so each record's bytes are bracketed individually:

| field | records walked | accepted | cursor delta | records whose commit pointer moved (list index → bytes) |
|---|---|---|---|---|
| 6445 | 104 | 9 world + 3 screen | 5068 | 0→184, 1→184, 2→200, 93→528, 97→824, 98→528, 100→2252, 101→368 |
| 6447 | 104 | 9 world + 3 screen | 5156 | same shape (±3%) |
| 6449-6455 | 104 | 9 world + 3 screen | 4932-5268 | same shape |

List indices 0, 1, 2 are the three gem records (matches, not inference: the in-process oracle logs the
same three `class 83` nodes at ordinals 0, 1, 2), so **retail's shaded pass advances its per-record
commit pointer for the gems** — 568 bytes across the three — and the native's 22-26 flat primitives
for those same records are faithful inclusion, not over-inclusion. (The other moving records are the
world objects at 93/97/98/100/101; ordinal 103 is accepted and moves it by nothing.) What that pointer
counts — GPU packet bytes or the pass's own per-record scratch, which the backward chaining loop at
0x80023990 walks in 8-byte entries — is not established, and this section does not depend on it: the
record gate is what it settles.

### After the fix

With the flat untextured family decodable, the same capture shape gives, per frame,
`0x80022A2C: 22-26 submitted / 22-26 matched / 0 unmatched`, `retail only: 0`, and the native-only
residue back to the pre-existing single regular-pass primitive. `refused` falls from 33 to 8. The
colour, semi-transparency and depth rules this issue changed are now **parity-verified** against
retail's own packets rather than merely source-grounded.

`test_gpu_packet_decode` pins the families: Gouraud untextured keeps per-vertex colour, flat
untextured carries one colour to every vertex, the quad/three-vertex bit selects four vertices, and
textured Gouraud still reads its texcoords and texture pages/clut. Non-polygon commands and
size-mismatched tags must stay refusals.

## Residuals

- **The flat textured family (`0x24`/`0x26`/`0x2C`/`0x2E`) is refused by name**, and the oracle prints
  its codes: 8 packets per frame, all `0x26`, sitting in the world OT at the gems' screen positions.
  Their three-vertex size is fixed (32 bytes) but the order of the colour, XY and texcoord words is
  not established for this title. A guessed `(XY, UV)` interleaving was tried and **falsified**: it
  moved 16 retail primitives out of the matched set and broke the shadow arm's matches, so it was
  reverted. Establish the layout from the code that writes these packets first.
- The `0x80059A48` (Spyro's shadow) match rate varies by capture — 16/16 on one Artisans capture and
  16/0 on another at the same frame index — so the shadow arm needs a phase-stable comparison before
  anything is claimed about it. The captures differ in camera phase (the level's opening pan), so
  this is not evidence of a defect either way.
- `TRZ` (see earlier) and the depth-ordering question (issue 0105) are unchanged.
