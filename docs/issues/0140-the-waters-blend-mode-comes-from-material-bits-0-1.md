---
id: 0140
title: The water's blend mode comes from material bits 0-1, and the "50/50" claim holds only when they are clear
status: open
symptom: the pool-water fix (c229e45) reconstructs the guest's constant colour word as
  `0xe1000600u | ((materialWord & 7u) << 5)` and its comment states the result is "a 50/50
  semi-transparent black". That is correct per the GP0 semi-transparency code, but the port does NOT
  take the blend mode from that command word — it derives it from a DIFFERENT pair of material bits,
  via a port-invented `tpage` encoding. The two agree only when those bits are clear.
tags: render,blend,water,fps60
created: 2026-09-28
updated: 2026-09-28
---

## The two mechanisms, which are not the same field

**The guest's command word.** `0x8002654C ori $a1,$zero,0xe100`, stored as the primitive's word 0. In
GP0 the top byte carries both the primitive kind and the semi-transparency code in bits 27..24:

    command byte 0xE1 = 1110_0001
      bit 27 (byte bit 3) semi-transparency flag = 1
      bit 24 (byte bit 0) polygon                = 1
      code nibble bits 27..24 = 0x1  ->  B/2 + F/2, i.e. 50/50

So the comment's "50/50" is **right about the guest**.

**The port's blend mode.** The rasterizer reads the texpage's ABR field:
`psxport/runtime/psx/gpu_native_raster.cpp:50 blend555(bg, fr, fg, fb, mode)` with four named modes —
`B/2+F/2` average, `B+F` additive, `B-F` subtractive, `B+F/4` additive/4 — where `mode` is
`s_tp_blend`, taken from `(tp >> 5) & 3` at `gpu_native.cpp:362`. And the LQ world path supplies that
texpage from a **port-invented encoding**, `game/render/world_lq_recipe.cpp:214`:

    face.material.tpage = (uint16_t)((material & 3u) << 5);

consumed by `game/render/world_scene_submitter.cpp:196` as `(face.material.tpage >> 5) & 3u`.

**So the blend mode is `material & 3` — material bits 0 and 1 — while the colour constant consumes
bits 0..2 via the `<< 5`.** The AR and the semi-transparency selector are read from one word but are
different fields, and the port conflates the tpage encoding with the guest's DR_MODE.

## The measured consequence

    material=0 -> tpage=0x0000 -> mode 0  B/2+F/2  average      <- agrees with code nibble 1
    material=1 -> tpage=0x0020 -> mode 1  B+F      additive
    material=2 -> tpage=0x0040 -> mode 2  B-F      subtractive
    material=3 -> tpage=0x0060 -> mode 3  B+F/4    additive/4
    material=7 -> tpage=0x0060 -> mode 3  B+F/4    additive/4

**Where the two mechanisms agree is only `material & 3 == 0`.** That is presumably the common case for
pool water, and it is why the constant's 50/50 reading is not wrong — but it is *not* established, and
the code comment asserts the blend from a field the port does not consult.

**THE TEST FIXTURE DOES NOT REPRESENT REAL WATER.** `tests/test_world_scene_prepare.cpp` uses
`materialWord = indices | 7`, so its blend mode is **3 (B+F/4)**, not 0. The fixture therefore pins
the correct *colour* on a face whose *blend* is not the one the comment describes. That is acceptable
for testing the colour branch, and it is a reason the comment's blend claim was never caught.

## What this does NOT claim

**Not a shipped defect.** Every other surface reads the gouraud arm and is reported clean, and no
measured frame shows a wrong-blend water. This is a **claim that is currently unestablished**, recorded
because a comment in shipping code asserts a blend the code does not derive, and the next person to
trust it will be misled.

## What would settle it

The authored material bits 0..1 of real pool-water faces, read from the disc:

- **If they are clear (`& 3 == 0`) for every water face** — the port's derived mode is 0 = average, it
  matches the guest's code nibble 1, and the comment is right by coincidence of encoding. The honest
  follow-up is to say *why* it is right (the two encodings coincide) rather than assert 50/50.
- **If any water face sets bits 0..1** — the port blends it in a mode the guest never asked for, and
  that is a real visual defect that the colour fix does not address.

`tools/census_level_blend.py` already walks the same `WAD.WAD` records and the same `materialWord`
field, so this is one added histogram over data already parsed.

**Falsifier:** a water face with `(materialWord & 3) != 0` in the provisioned data refutes "the two
mechanisms agree"; every water face with `(materialWord & 3) == 0` confirms it and downgrades this to
a comment-accuracy issue.
