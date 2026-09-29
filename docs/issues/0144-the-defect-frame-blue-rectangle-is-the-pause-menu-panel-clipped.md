---
id: 0144
title: The defect frame's blue rectangle is the pause menu's panel, clipped to 147 of its 232 columns by the port's own widescreen clip
status: open
symptom: `docs/issues/0143` established that the blue in the defect frame is a screen-space 50%-blend
  of constant RGB(0,56,192) and named two unattributed screen-space producers, `func_80018F30` and
  `func_8001860C`. Both are excluded, and neither is the answer: the rectangle is the **pause menu's
  own panel quad**, emitted by `spyro::pause_menu_scene::submitPanel` under guest handler
  `0x8001A40C`, and it is painted **147 columns wide instead of 232** because the clip the port
  gives it is a guest-space constant used as a drawn-space bound.
tags: render,overlay,pause,widescreen,probe
created: 2026-09-28
updated: 2026-09-28
---

## 1. Which producer draws it: 1 of 4 candidates tried owns it, and it is not one of the two named

`PSXPORT_MUTE_FN`, which 0143 names as the instrument, **no longer exists**. It was removed from
`spyro/core/native_render.cpp:149` by psxport `06993273` ("Replace static recompilation with
Lightrec runtime", 2026-09-04); `grep -rn MUTE_FN` over the whole framework tree returns nothing.
Two of the claims that used it (C138, C147) are therefore no longer reproducible as written. The
substitutes below are all still present and all report their denominators.

**(a) The two named candidates are excluded by the guest's own bytes, not by a mute.**

- **`func_80018F30`, the screen border.** `0x80018F34` reads `[0x8007570C]`
  (`g_ScreenBorderEnabled`) and `0x80018F58` reads `[0x800756C0]`; `0x80018F9C` clamps it with
  `addiu $v0,$zero,0x16` behind `sltiu $v0,$v1,0x17` at `0x80018F60`, so the bar height is **at
  most 22 rows**. `0x80018F98` materialises the tag `0x05000000` and `0x80018FAC` the code `0x28`,
  over `x0=0, x1=512` — a full-width black letterbox. The measured rectangle is **110 rows** tall
  and spans 147 of 232 columns. It cannot be this.
- **`func_8001860C`, the shaded box.** `0x80018614`, `0x8001861C`, `0x80018624`, `0x8001862C` move
  `$a0..$a3` straight into `$t3`: the body holds **no geometry of its own**. A scan of all
  **103,936** main-image words (`0x80010000..0x80075800`) finds exactly **one** `jal` to it,
  `0x8001D7C8`, whose four arguments are `$v1 + <computed>` from a table — a different screen, with
  coordinates that are not 140/372/67/176. The three `jal` sites per level overlay (the balloonist
  HUD) pass `(72,440,28,104)`, `(224,472,25,51+n)` and `(72,440,28,102)`. It cannot be this either.

**(b) The owner, measured.** `PSXPORT_PRIMRGB=0,56,192` (tolerance ±24) over the pause route:

    [primrgb] f3460 MATCH seq=2118 layer=3 om=2 mode=3 nv=4 semi=1 v0=(0,57,198)
                    bbox=(226,67)-(458,176) display_bbox=(226,67)-(458,176) depth0=0.000000
    [primrgb] f3460 scanned 4264 prim(s), 2 carried rgb(0,56,192) +-24

**2 of 4264** prims per frame, always the same two (one per field), always with
`layer=3` = `RQ_HUD`, `om=2` = `RQ_OM_2D_FG`, `mode=3`, `nv=4`, `semi=1`. That tuple is the literal
argument list of one call site in the whole port: `pause_menu_scene::submitPanel`'s
`emitOrQueue(core, 1, RQ_HUD, RQ_OM_2D_FG, 4, kPanelStp, 0, ..., 3, 0, 0, 0, 0, ...)`. And the
bounding box is the guest's panel, `(140,67)..(372,176)` plus the +86 widescreen centring margin
(`(684-512)/2`). Over the whole run the census printed **3,068** frame reports: **3,025** carried 0
and **43** carried 2, and the 43 are the contiguous tail `f3460..f3502` — the pause. The zero is the
instrument's own discrimination, not an absence of scanning. Log:
`scratch/logs/sheet-ctl.log`.

**(c) The layer, measured by a mute that still exists.** `PSXPORT_NOHUD=1` drops `RQ_HUD` prims
(`render_queue.cpp:1040`). With it: **0 of 4252** prims carry the colour, and **0 of 8** presents show
the rectangle. The 13-column run that survives at the same guest x (`336..344` at 16:9,
`250..259` at 4:3) is Spyro's green glow — a world prim — which is the negative control that says
the detector is not simply firing on anything bluish.

**Denominator: 4 candidates tried, 1 owns it, 3 do not.** The 3 that do not are the 2 named by 0143
plus the world/water path, which `PSXPORT_NOHUD` removes by exclusion.

## 2. What it is FOR: the guest's own state, read from guest memory

Not inferred from the picture. The port's pause owner logs the words it reads
(`PSXPORT_DEBUG=render`, 34 samples over one pause):

    [render] pause-menu: frameCounter=0  page=0 gui=0 panel=(0,0)..(0,0)     border=0 captions=0
    [render] pause-menu: frameCounter=1  page=0 gui=1 panel=(140,67)..(372,176) border=5 captions=5
    ...
    [render] pause-menu: frameCounter=33 page=0 gui=1 panel=(140,67)..(372,176) border=5 captions=5

`[0x800758B8]` is the menu's own tick, `[0x800757C8]` its page, and `g_Gamestate` (`0x800757D8`)
reads **2 = GS_PauseMenu** in 7-9 of the driver's post-arrival census samples. `page=0` is the main
page, and `captions=5` is PAUSED + CONTINUE + OPTIONS + INVENTORY + QUIT GAME.

The guest's own bytes agree, at `0x8001A7C4-0x8001A864`: `sw 0x05000000` (POLY_F4 tag),
`addiu $v0,$zero,0x2A` + `sb` (the semi-transparent untextured command), `addiu $v0,$zero,0x43`
+ three `sb $s4` (y=67 and the three colour bytes), then the page branch, then `0x8C`/`0x174`/`0xB0`
= 140/372/176 for the non-options pages, then the four `func_8001844C` border lines.

**So the rectangle is the pause menu's panel, and the "second, unfilled outline box beside it" that
0143 could not attribute is the same panel's own right-hand border, 86 columns past where the fill
stops.**

## 3. Why the product is paused on arrival: the capture route pressed Start

`tools/drive.py --help` records the incident: *"gamestate 0 is both the boot logo and the arrival
state, and a live run that re-fired there pressed Start in gameplay and opened the pause menu."*
The logs from the captures that produced these screenshots say the same thing —
`scratch/logs/shot_run.txt` and `text_run.txt` both read
`gamestates over 661 samples: playing=57, 2=13, ...; since GS_Playing: playing=12, 2=13`.

A pause menu appearing in response to a Start press in gameplay is **correct behaviour**, not a
state bug. There is no evidence here of the product being in a state it should not be.

## 4. The port draws it, and it is not faithful in two measurable ways

`render_frame.cpp:298-306` dispatches `sc.stage == kStagePauseMenu` to `pause_menu_scene::submit`,
so **the guest's `0x8001A40C` never runs**; the port's native owner is the submitter. Its geometry
is byte-faithful — `(140,67)..(372,176)`, code `0x2A`, semi, 4 vertices, `SetDrawMode(1, 0, 0x40)`,
front-list `AddPrim`, the prim cursor advanced by 6 + 5·n words. Two things are not.

**(D1) THE FILL IS CLIPPED TO 147 OF ITS OWN 232 COLUMNS. Widescreen only.**

`submitPanel` passes `recipe.panelX1` as the draw-area clip's right edge
(`pause_menu_scene.cpp`, the `emitOrQueue` call's `da_x0, da_y0, da_x1, da_y1` arguments are
`gpu.s_da_x0, gpu.s_da_y0, recipe.panelX1, gpu.s_da_y1`). The queue rasterises that clip in the
**drawn** space, and the panel's vertices are emitted under
`RenderQueue::Space2dScope(RQ_2D_AUTHORED_4_3)`, which **centres** rather than stretches — so at
16:9 the vertices reach 458 while the clip still says 372. The 4:3 control, where the margin is 0
and the two spaces coincide, is unaffected. Measured with `tools/overlay_extent.py`:

| leg | settings | presents | panel on | painted span | own extent |
|---|---|---|---|---|---|
| 16:9 | `tools/shipping_settings.ini` (aspect=1) | 8 | **4 of 8** | **147** | 232 |
| 4:3 | `tools/narrow_only_control_settings.ini` (aspect=0) | 16 | **8 of 16** | **232 / 233** | 232 |
| 16:9 + `PSXPORT_NOHUD=1` | shipping | 8 | **0 of 8** | — | 232 |

`232/233` is 140..371 and 140..372 — the same quad, PSX fill rules, both the guest's extent. **147 is
147.** This is also the whole reason the artefact read as a *water sheet with an outline beside it*
rather than as a menu: a half-drawn box with its own intact border is not recognisable as a box.

**Root cause, and it is not a fitted offset:** the clip and the vertices are expressed in two
different spaces, and the value the port passes is a guest-space constant. The **proper fix belongs
in the render queue's `RQ_2D_AUTHORED_4_3` handling** — a prim submitted in that space should have
its `da_*` clip carried through the same transform its vertices get, so the only implementation of
the centring stays in the one owner that has it. The local alternative, which is correct and small
but leaves a second place that has to know the margin, is to give the panel the same clip
`submitBorder` already receives (`drawAreaX1`, the guest draw area's right edge, `cx + cw - 1` from
`render_frame.cpp:299`) instead of its own geometry. **I recommend the queue-side fix and have not
shipped either**, because both are framework-facing and this claim is `scratch`-scoped.

**(D2) THE PANEL'S COLOUR IS THE WRONG CONSTANT.** `pause_menu::kPanelColourByte = 0xE0`, cited in
`pause_menu_recipe.h` as "the guest's own three stored bytes (0x8001A7CC-0x8001A7E0)". The addresses
are right; the value is not. The panel's colour bytes are `sb $s4, 0x48/0x49/0x4A($s0)` at
`0x8001A7D8-0x8001A7E0`, so the colour is **whatever `$s4` holds there**. Scanning all **2,212**
words of the handler body (`0x8001A40C..0x8001C69C`) for every write to `$s4`, the only two before
that point are `addiu $s4,$zero,0x00E0` at `0x8001A450` — the delay slot of `jal 0x80019698`, on the
**world** path — and `addiu $s4,$zero,0x0040` at `0x8001A6C8` — the delay slot of `lbu $a1,0x0D($s0)`
inside the unconditional four-tiled-quad loop that precedes the panel. The latter is last, so
**retail's panel colour byte is `0x40`**, which the vendored decompilation states correctly as
`setRGB0(f4, 64, 64, 64)` (`external/spyro-1/src/gamestates/draw.c:990`). `0xE0` is a dead
register write the guest never reads back.

    guest  0x40 x3 -> 0x00404040 -> 5-bit (0,2,16)  -> rgb(0, 16, 132)
    port   0xE0 x3 -> 0x00E0E0E0 -> 5-bit (0,7,24)  -> rgb(0, 57, 198)   <- measured in the picture

The measured source in the defect frame is rgb(0, 56, 192) — the **port's** value, to within blend
rounding. So this one is a genuine fidelity defect, it makes the panel markedly brighter and more
saturated than retail's, and it is the second half of why the artefact looked wrong. It is not
what put the rectangle on the screen.

## 5. The cadence, which is why half the operator's captures disagree with each other

`pause2.png` and `pause3.png` (no rectangle) and `pause-final.png` and `field-16x9-interp.png`
(rectangle) are the **same gamestate one present apart**. Measured over consecutive presents with
`tools/drive.py --preseq` and read by `tools/overlay_extent.py`:

- the panel is submitted on **33 of 33** menu frames (`pause-menu: frameCounter=1..33`, all
  `gui=1`), and `PSXPORT_PRIMRGB` finds it on **every** census frame from 3460 to 3503;
- it is on the picture in **4 of 8** presents at 16:9 and **8 of 16** at 4:3 — a 2-on/2-off cadence in
  **both** aspects, so it is not a widescreen artefact;
- the whole 2D overlay blinks together: an off present shows no panel, no border and no caption
  (`scratch/sheet/c-dbg0.png` against `c-dbg2.png`).

0143's "**0 of 104** captures from 7 independent runs produced it" is this cadence, not a broken
route. The leading mechanism is double buffering: `PSXPORT_PRIMRGB` reports the panel's
`display_bbox` y alternating **67 / −173** while its raw `bbox` y stays 67, i.e. the quad is submitted
at an absolute framebuffer y on both fields while `s_disp_y` alternates 0/240, so on the field whose
display origin is 240 the quad is rasterised into the other buffer's rows. **I did not measure
`s_off_y`/`s_disp_y` at the flush**, so this is a hypothesis, not a result.

## What this does NOT claim

- **Not a claim that the pause menu is wrong to be there.** §3: it is the product's correct response
  to the Start press the capture route issued.
- **Not a claim about the world path.** 0143's §2 census stands untouched: 0 of 1783 low-poly faces
  in Artisans carry material bit 2, and `c229e45` remains a no-op for this level. That negative was
  correct and is not revised here.
- **Not a mute-based exclusion of the two named candidates.** They are excluded by the guest's own
  bytes, with the geometry and colour each one is capable of stated above. A mute would have been a
  second opinion on a conclusion the bytes already carry; the instrument for it does not exist.
- **Not a claim that `PSXPORT_PRIMDUMP` works.** It is declared in
  `gpu_primitive_dump.cpp` and the product's own env audit prints
  `UNKNOWN knob PSXPORT_PRIMDUMP was set for this whole run and NOTHING ever read it` on every run
  that sets it. `PSXPORT_PRIMRGB` and `PSXPORT_NOHUD` are the instruments that do read.
- **Not a shipped fix.** §4's D1 and D2 are diagnosed with the change named; neither is applied.

## What would settle it

1. **D1's fix, measured:** after the queue-side clip transform lands, `tools/overlay_extent.py` over
   a 16:9 `--preseq` strip must report span 232 on every present that carries the panel. Anything
   less is a partial fix.
2. **D2's fix, measured:** `PSXPORT_PRIMRGB` over the pause route must then match `rgb(0,16,132)`±24
   and report `0 of N` for `rgb(0,56,192)`. If it reports both, the panel is being submitted twice
   with two colours, which is a different defect.
3. **§5's mechanism:** read `s_off_y` and `s_disp_y` at the render-queue flush for one HUD prim, on
   both fields of one game frame. A `s_off_y` of 0 on both fields confirms it; 0 and 240 refutes it
   and the cause is elsewhere in the present path.
4. **The colour claim under a second eye:** the `0xE0` at `0x8001A450` is a delay slot on the
   **world** arm, which the guest leaves by returning; a path that re-entered the handler mid-frame
   without passing `0x8001A6C8` would make `0xE0` live. Nothing in the 2,212-word body suggests one,
   and the decompilation's `64` is independent, but a `func_8001A40C` call trace would close it.

## Reproduction

    # 16:9, the shipping enhancement configuration
    rm -rf scratch/sheet/strip && mkdir -p scratch/sheet/strip
    uv run --frozen python tools/drive.py gameplay --tap start --after 60 \
        --preseq 8 --preseq-dir scratch/sheet/strip \
        --debug render --env PSXPORT_PRIMRGB=0,56,192
    uv run --frozen python tools/overlay_extent.py scratch/sheet/strip/p*.ppm

    # 4:3 control, same route
    rm -rf scratch/sheet/strip43 && mkdir -p scratch/sheet/strip43
    uv run --frozen python tools/drive.py gameplay --settings tools/narrow_only_control_settings.ini \
        --tap start --after 60 --preseq 16 --preseq-dir scratch/sheet/strip43
    uv run --frozen python tools/overlay_extent.py scratch/sheet/strip43/p*.ppm

    # the layer mute
    rm -rf scratch/sheet/stripnohud && mkdir -p scratch/sheet/stripnohud
    uv run --frozen python tools/drive.py gameplay --tap start --after 60 \
        --preseq 8 --preseq-dir scratch/sheet/stripnohud \
        --env PSXPORT_NOHUD=1 --env PSXPORT_PRIMRGB=0,56,192

`--preseq-dir` is **not** cleared for you, on purpose: a stale `p0000.ppm` in it is read as one of
the N presents, which is the C138 failure. Delete the directory before every run, as above.

## 2026-09-29: D2 fixed, D1 open

D2 (panel colour) is fixed: the byte is read from the guest's `addiu $s4,$zero,imm` at 0x8001A6C8
(retail 0x40, decomp `setRGB0(f4, 64, 64, 64)`), gated by `tools/probe_pause_panel_colour.py
--selftest` and `test_pause_menu_recipe`. D1 (the 147-of-232-column clip at 16:9) is open. Its cause
is psxport `RenderQueue::emitOrQueue` transforming a 2D prim's vertices through `Rq2dXform` but not its
`da_*` clip; the fix belongs there and must be gated across every title's 2D producers. A title-side
stopgap was drafted and not landed without approval.
