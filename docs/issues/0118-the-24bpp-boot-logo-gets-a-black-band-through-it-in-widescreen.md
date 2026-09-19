---
id: 118
title: The 24bpp Universal boot logo gets a black band through it in widescreen
status: resolved
symptom: at 16:9 the Universal Interactive Studios boot logo is cut by an opaque black band at display columns 342..454, while the widened margin beside the picture is never covered
state_items: S019
tags: widescreen, presentation, 24bpp, psxport
created: 2026-09-19
updated: 2026-09-19
---

## What happened

`docs/project-state.md` S019 recorded the `secondary` f399 checkpoint as "no gain — not identified",
treating it as a benign measurement. It is the Universal Interactive Studios boot logo, and it was
visibly broken at 16:9: an opaque black band straight through the middle of the picture, cutting the
globe and wordmark and punching a hole in the copyright line.

## Mechanism

The logo is an upload-only guest-VRAM picture (`SpyroPresentationOwner`, `Owner::GuestVram`): the
guest uploads pixels and presents them, emitting no draw primitives at all, so there is no ordering
table to walk and nothing title-side is involved in putting it on screen.

The guest programs GP1(08) = 08000012 — 512x240, **24-BIT** — and the port widens the display to 684.
psxport's `plan_wide_margin` built its coverage rect as `[sx + native_w, sx + wide_w)` in VRAM
halfwords, directly from *display* widths. At 15bpp that is correct, because one display pixel is one
halfword. At 24bpp a pixel is RGB888 packed across **1.5** halfwords — `present.frag` reads display
column x at byte `disp.x*2 + x*3` — so halfwords [512,684) are sampled as display columns
[341,456). That is the band, and the real margin (display columns 512..684, halfwords 768..1026) was
never covered.

## Evidence

- Reproduced on a current build; the earlier scratch capture was stale (02:19 vs a 10:41 build).
- VRAM is **byte-identical** between the 4:3 and 16:9 legs (0 of 524288 words differ), so nothing
  guest-side diverged — this was purely a presentation defect.
- Both legs program the same GP1(08)=08000012.
- Not a 1555-vs-24bpp decode failure: synthesising both decodes from the VRAM dump gave mean |diff|
  8.16 for 24bpp against 108.41 for 1555, falsifying that hypothesis outright.
- The band was measured at display columns 342..454, against 341.3 (`512*2/3`) and 456.0 (`684*2/3`)
  predicted. Outside it the picture matched a correct 24bpp read to mean |diff| **1.29**.

The logo's left-anchoring is a separate, still-holding constraint — claim C143, psxport's 2D widen is
deliberately disabled on this port — and is not part of this defect.

## Fix

psxport `ac3d1db4`. `plan_wide_margin` takes the display depth and converts display columns to VRAM
halfwords, clamping to VRAM's 1024-halfword width (which this case reaches: 684 columns need halfword
1026). Seeding the old identity conversion back in fails 4 of the 7 tests in psxport's
`tests/test_wide_margin_plan.cpp` — the old test certified the 15bpp answer for exactly this input,
which is why the 24bpp case went uncovered. The drawing moved to `runtime/psx/gpu_vk_wide_margin.cpp`.

## Verification

Rebuilt against psxport `ac3d1db4` and re-captured f399 in both aspects
(`external/psxport/tools/port/looks_right.py --frames 400 --shot-at 399`). Measuring interior
all-black column runs in the 960x720 sink:

| build | 16:9 content columns | interior black runs > 3 px |
|---|---|---|
| pre-fix (`scratch/logo-repro/aspect-16x9`) | 20..704 | **480..639**, 662..668 |
| fixed (`scratch/logo-fix/aspect-16x9`) | 20..704 | 662..668 |

The 160-px band at 480..639 is display columns 342..455 — the predicted position — and it is gone.
The 662..668 run appears in both legs, so it is part of the logo art and not a defect. The 4:3 leg is
unchanged (content 26..940, its own two art runs at 113..123 and 885..893). Both legs carry the same
487 display columns of picture, so the logo body is intact.

## What this does NOT fix

`coverage` still reports NO GAIN (drawn aspect 1.466 -> 1.470) for this scene, and that is now
explained rather than unexplained. The logo is an upload-only guest-VRAM picture: there is no
geometry to widen, and the port's 2D widen is deliberately disabled (claim C143, 2D-vs-3D
discrimination rides on per-primitive depth at ~2.5% coverage). So it is left-anchored inside a
correctly black margin. Making a 2D-only boot picture fill 16:9 is a separate, larger item gated on
C143.

## Oracle parity on the fixed framework

Re-run after the pin bump to psxport `ac3d1db4`, with both enhancements live
(`--frame-step 15 --product-env PSXPORT_FPS60=1 --product-env PSXPORT_SETTINGS=<aspect=1>`): **36
checkpoints, 468 decisive range comparisons, 0 divergences, `complete: true`**. The product's own log
proves the enhancements were not silently ignored — `[fps60] TRUE per-object interpolated 60fps ON`
and `[wide] native picture: aspect=1 wide_engine=1 native_width=512 render_width=684` — and the
comparator was shown the other answer first: `--selftest` seeded a byte at `0x80078A58` and DETECTED
it. So the margin change writes nothing into guest state.
