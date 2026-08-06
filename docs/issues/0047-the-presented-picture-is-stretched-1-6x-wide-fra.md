---
id: 47
title: the presented picture is stretched 1.6x wide — framebuffer aspect is used as display aspect (mirror of spider1 #8)
status: open
symptom: everything on screen is too wide and too short; spyro's 512x240 picture presents as a 2.133:1 band inside the sink with big top/bottom black bars instead of filling a 4:3 window
tags: render,present,aspect,letterbox,framework,all-ports,mirror
created: 2026-08-06
updated: 2026-08-06
---

MIRROR OF spider1 `docs/issues/0008-the-presented-picture-is-stretched-1-6x-wide-fra.md`.
That issue is tagged `all-ports`, it is RESOLVED THERE and UNFIXED HERE, and until now spyro's
catalog had no entry for it at all — so a spyro session searching its own registry for the symptom
got "(no matches)" over a 46-entry corpus and would have re-derived the whole thing. Both entries now
point at each other; neither repo can lose the link again.

## Root cause

`show_composite()` letterboxes the game picture to the FRAMEBUFFER's dimensions instead of to the
DISPLAY aspect. VERIFIED IN THIS TREE (spyro `external/psxport` @ `dbc5a5e1`, CLEAN, not inferred
from spider1):

    external/psxport/runtime/recomp/gpu_vk.cpp:1047
        SDL_GPUViewport vp = letterbox(disp_w, 240, (int)sw, (int)sh);

The algebra that names the defect:

    disp_w / 240  ==  (4/3) x (disp_w / 320)

so `320` is hardcoded as EVERY game's native 4:3 width. On PSX the horizontal framebuffer width
(256/320/368/512/640) selects the horizontal SAMPLING RATE, not the shape of the picture — every one
of those modes scans into the same 4:3 screen area, so pixels are non-square (0.625:1 at 512 wide).
Presenting a 512x240 frame at its literal 512:240 aspect therefore stretches it 1.600x horizontally
and manufactures top/bottom bars that should not exist.

    Tomba2 native      disp_w=320  -> 4:3            CORRECT (320 IS its native width)
    Tomba2 widescreen  disp_w=428  -> 1.783 ~ 16:9   CORRECT
    spyro   native     disp_w=512  -> 2.133          WRONG, should be 4:3
    spider1 native     disp_w=512  -> 2.133          WRONG

Spyro is 512x240, so spyro presents a 960x450 band inside a 960x720 sink: 1.600x too wide, 135 px of
black above and below.

THE SAME FILE ALREADY CONTAINS THE CORRECT FORM, which is why this is a defect and not a design
choice: `gpu_vk_present_image()` — the RGBA still-image path — letterboxes with `letterbox(4, 3, sw, sh)`
at `gpu_vk.cpp:1128`. So on spyro the two present paths disagree, and the picture VISIBLY CHANGES
SHAPE at every image/FMV to gameplay transition: the still-image path fills the sink, the game
composite sits in a band with bars. That is a testable prediction about this port, checkable from
any two captures taken either side of such a transition.

## Status in THIS repo: UNFIXED

`external/psxport` here is `dbc5a5e1` and is CLEAN. It has no `runtime/recomp/present_plan.h` at all,
`grep -rn native_w runtime/recomp/gpu_vk.cpp` finds only `gpu_vk_native_w` / `wide_native_w`
(the WIDESCREEN accessor, a different consumer), and the present-side `PresentInputs.native_w` field
that fixes this DOES NOT EXIST in this tree. Verified 2026-08-06.

The fix therefore has to arrive here as a framework pin bump, not as a spyro-side change.

## The fix (already designed, tested and landed in spider1's checkout — do not re-derive it)

`PresentInputs` gains `native_w`, the game's own 4:3 framebuffer width taken from the DISPLAY
REGISTER (`gpu.s_disp_w`, i.e. the pre-widening width — passing the already-widened `disp_w` would
restore the stretch), and the letterbox becomes

    pane_letterbox(4 * disp_w, 3 * native_w, sink_w, sink_h)

i.e. 4:3 scaled by how much wider than ITS OWN native width the framebuffer is. `native_w <= 0`
degrades to 4:3, which is both the right default for every PSX mode and divide-by-zero-proof.

It is a provable no-op for a 320-wide game: at `native_w == 320`, `4*disp_w : 960 == disp_w : 240`,
algebraically identical to the old rule. spider1's `tests/test_present_plan.cpp` asserts that against
a literal transcription of the old rule for all 385 widths in 256..640, x/y/w/h each.

## What is NOT established here, stated so nobody reads this as verified

* **This has never been measured on spyro.** spider1 measured its own before/after (450-tall band,
  2.133:1, STRETCHED 1.600x -> 720-tall, 1.333:1, fills the sink). spyro's number is DERIVED from
  the code above and from spyro being 512x240; no spyro capture has been put through a shape check.
* **The shape instrument needed repairing before it could measure this port, and the repair is in
  this repo.** The ORIGINAL `present_geometry.py` (still what `spider1/tools/` carries) measures the
  NON-BLACK CONTENT BOUNDING BOX rather than the display rect, and on a spyro frame it printed a
  confident **`STRETCHED 1.714x`** where the real present stretch is **1.600x**. The error is exact
  and names its own cause: `1.714 / 1.600 == 240 / 224`, and spyro's guest draws 224 of its 240
  display lines, so 16 rows are black BECAUSE THE GAME DREW THEM BLACK — which a pixel-only band
  measurement charges to the letterbox. `tools/present_geometry.py` IN THIS REPO (instrument **I042**,
  `--selftest` 16/16) is the repaired copy: it REFUSES with rc=3 on that ambiguity instead of
  guessing, and resolves the same frame to `1.600x` when given `--active 512x224 --display 512x240`.
  **Use this repo's copy, pass the guest extent, and never quote a bare band aspect.** spider1's copy
  is stale — check with `md5sum */tools/present_geometry.py` from `~/repo/psx` before trusting a
  number from either.
* Nothing here says spyro's flicker, pacing, or geometry problems are related to this. They are not
  known to be.

## Blast radius for spyro's own past evidence

Every spyro present-stage capture taken so far was taken through this stretch. Coverage percentages,
colour counts, brightness and per-tile richness are ALL INVARIANT under a uniform rescale, so those
numbers are not invalidated — but any judgement about SHAPE, position, bar placement or "how much of
the sink is filled" made from a spyro present shot was made on a stretched image. Issue 0039
(widescreen band) and issue 0037 in particular reason about horizontal extent.

## Cross-references

* spider1 `docs/issues/0008-*` — the full RE, the algebra, the test matrix, the fix, and the
  measured before/after. THE CANONICAL RECORD. Its own closing note says: "NOT VERIFIED: spyro."
* spider1 `docs/info/instruments.md` INST-25 — `present_geometry.py`, and its content-bbox limit.
* This tree: `external/psxport/runtime/recomp/gpu_vk.cpp:1047` (defect) and `:1128` (the correct
  form, same file).
