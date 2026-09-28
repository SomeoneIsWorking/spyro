---
id: C230
kind: claim
status: holds
created: 2026-09-28
tags: render,overlay,pause,widescreen,probe
depends: tools/overlay_extent.py, tools/drive.py, tools/probe_guest_disasm.py
---

## Claim

The blue screen-space rectangle in the operator's defect frame is the **pause menu's panel quad**,
submitted by `spyro::pause_menu_scene::submitPanel` under guest draw handler `0x8001A40C` (guest
`g_Gamestate` `0x800757D8` = 2 = `GS_PauseMenu`, guest menu page `[0x800757C8]` = 0), and it is
painted **147 of its own 232 columns under widescreen and all 232 at 4:3**. The two producers
`docs/issues/0143` named, `func_80018F30` and `func_8001860C`, are both excluded by the guest's own
bytes: the border's bar height is clamped to 22 rows over the full width, and the shaded box holds
no geometry at all and has one main-image caller with table-computed coordinates. The fill is cut
because `submitPanel` passes `recipe.panelX1` (372) as its draw-area clip in the **drawn** space
while its vertices are emitted in the authored 4:3 space that the widescreen margin has already
shifted to 458. The panel's colour is a second, independent fidelity defect: retail stores
`$s4 = 0x40` at that point and the port hard-codes `0xE0`, a dead register write from the world arm.

## Evidence

**The owner, by colour census, with a denominator.**
`PSXPORT_PRIMRGB=0,56,192` (tolerance ±24) over `tools/drive.py gameplay --tap start --after 60`:

    [primrgb] f3460 MATCH seq=2118 layer=3 om=2 mode=3 nv=4 semi=1 v0=(0,57,198)
                    bbox=(226,67)-(458,176) display_bbox=(226,67)-(458,176) depth0=0.000000
    [primrgb] f3460 scanned 4264 prim(s), 2 carried rgb(0,56,192) +-24

**2 of 4264** prims per frame, both with `layer=3` (`RQ_HUD`), `om=2` (`RQ_OM_2D_FG`), `mode=3`,
`nv=4`, `semi=1` — the literal argument list of exactly one `emitOrQueue` call site in the port, and
the bounding box is the guest's `(140,67)..(372,176)` plus the `(684-512)/2 = 86` centring margin
that `RQ_2D_AUTHORED_4_3` applies. The census printed **3,068** frame reports over the run:
**3,025** carried 0 and **43** carried 2, and the 43 are the contiguous tail `f3460..f3502` — the
pause. So the match is a property of the pause frames, not a constant the instrument emits.
(`scratch/logs/sheet-ctl.log`; the run that also carries the guest-state lines is
`scratch/logs/sheet-dbg.log`.)

**The layer, by a mute that still exists.** `PSXPORT_NOHUD=1` (`render_queue.cpp:1040` drops
`RQ_HUD`): **0 of 4252** prims carry the colour and **0 of 8** presents show the rectangle. The
13-column run that survives at the same guest x — `336..344` at 16:9, `250..259` at 4:3 — is Spyro's
green glow, a world prim, and is the control that says the detector is not firing on anything
bluish.

**The width, over consecutive presents, both aspects.** `tools/drive.py --preseq` + the new
`tools/overlay_extent.py`:

| leg | settings | presents | panel on | painted span | own extent |
|---|---|---|---|---|---|
| 16:9 | `tools/shipping_settings.ini` (aspect=1) | 8 | 4 of 8 | **147** | 232 |
| 4:3 | `tools/narrow_only_control_settings.ini` (aspect=0) | 16 | 8 of 16 | **232 / 233** | 232 |
| 16:9 + NOHUD | shipping | 8 | **0 of 8** | — | 232 |

The 4:3 span of 232/233 is `140..371`/`140..372`: the guest's own extent, both PSX fill rules. The
16:9 span of 147 is `226..372`. **Same route, same frames, one knob.**

**The guest's own state, not the picture's.** `PSXPORT_DEBUG=render` over one pause, 34 samples:

    [render] pause-menu: frameCounter=0  page=0 gui=0 panel=(0,0)..(0,0)        border=0 captions=0
    [render] pause-menu: frameCounter=1  page=0 gui=1 panel=(140,67)..(372,176) border=5 captions=5
    [render] pause-menu: frameCounter=33 page=0 gui=1 panel=(140,67)..(372,176) border=5 captions=5

`[0x800758B8]` is the menu's own tick, `[0x800757C8]` its page, and the driver's census reads
`g_Gamestate` = 2 in 7-9 of the post-arrival samples. `captions=5` is PAUSED + CONTINUE + OPTIONS +
INVENTORY + QUIT GAME. The guest's bytes at `0x8001A7C4-0x8001A864` build the same quad: tag
`0x05000000`, command `0x2A`, `addiu $v0,$zero,0x43` (y=67), the page branch, then `0x8C`/`0x174`/
`0xB0` = 140/372/176, then the four `func_8001844C` border lines.

**The two named candidates, excluded on geometry rather than by a mute.** All 103,936 main-image
words (`0x80010000..0x80075800`) were scanned for `jal` targets.
`func_80018F30`: `0x80018F9C` clamps `[0x800756C0]` with `addiu $v0,$zero,0x16` behind
`sltiu $v0,$v1,0x17` at `0x80018F60`, so its bars are **at most 22 rows** and full-width
(`x0=0, x1=512`); the measured rectangle is **110 rows**. `func_8001860C`: `0x80018614`-`0x8001862C`
move `$a0..$a3` into `$t3`, so the body has no geometry; its **1** main-image `jal` is `0x8001D7C8`,
with `$v1 + <table>` arguments, and the 3 per level overlay are the balloonist HUD at
`(72,440,28,104)`, `(224,472,25,51+n)`, `(72,440,28,102)`.

**The colour constant, over the whole handler body.** All **2,212** words of `0x8001A40C..0x8001C69C`
were scanned for every write to `$s4`. Before the panel's colour stores
(`sb $s4, 0x48/0x49/0x4A($s0)` at `0x8001A7D8-0x8001A7E0`) there are exactly two:
`addiu $s4,$zero,0x00E0` at `0x8001A450` (the delay slot of `jal 0x80019698`, on the **world** arm)
and `addiu $s4,$zero,0x0040` at `0x8001A6C8` (the delay slot of `lbu $a1,0x0D($s0)` inside the
unconditional four-tiled-quad loop). The latter is last, so retail's byte is `0x40`, which the
vendored decompilation independently states as `setRGB0(f4, 64, 64, 64)`
(`external/spyro-1/src/gamestates/draw.c:990`). `pause_menu::kPanelColourByte = 0xE0` is the dead
world-arm write. `0x40`x3 = `0x00404040` = 5-bit (0,2,16) = rgb(0,16,132); `0xE0`x3 = `0x00E0E0E0` =
5-bit (0,7,24) = rgb(0,57,198), and the picture measures rgb(0,56,192) — the port's.

**The instrument's offset formula was re-verified before any address was read.**
`uv run --frozen python tools/probe_guest_disasm.py --verify-only`:
`scanned 62183 MAIN-IMAGE CODE listing instruction(s) ... 62183 agree with
file_offset = 0x800 + (addr - 0x80010000); 0 disagree`.

**The instrument this brief named no longer exists, which is a fact about the repository and not
about the engine.** `PSXPORT_MUTE_FN` was `spyro/core/native_render.cpp:149` and was removed by
psxport `06993273`; `grep -rn MUTE_FN` over the framework returns nothing. C138 and C147 are
therefore not reproducible as written. `PSXPORT_PRIMDUMP` is also dead — the product's own env audit
prints `UNKNOWN knob PSXPORT_PRIMDUMP was set for this whole run and NOTHING ever read it`.

**New first-party capability, both halves registered.** `tools/overlay_extent.py` measures how many
columns of a submitted 2D quad reach the picture, over a strip, and reports `N of M` presents
because a screen-space overlay is submitted per field into a double-buffered picture, so a
single-frame answer is a coin flip that reads like a result. `tools/drive.py` gained `--preseq N
[dir]`, which drives the product's own `preseq` REPL command; it deliberately does **not** clear the
directory, so a stale `p0000.ppm` is visibly the operator's problem rather than a silently counted
present (C138). `overlay_extent_selftest` is registered in CTest and its negatives are the ones that
matter for a detector: a clean frame, a 13-column world glow the same statistic fires on, scattered
hits, an overlay clipped to 146 of 232, a strip on 3 of 6 presents, and three refusals (no frames, a
non-P6 file, a truncated PPM) none of which may read as a clean zero.

## What would falsify it

- **A 4:3 leg that also paints 147.** The clip diagnosis rests entirely on the 4:3 control being
  full width. If `narrow_only_control_settings.ini` ever paints 147 too, the cause is not the
  authored-4:3 margin and the whole of §4/D1 falls.
- **A 16:9 leg that paints 232.** Same in the other direction: the centring margin would have stopped
  being applied, and D1 would be a fixed symptom of a different change.
- **`PSXPORT_PRIMRGB` matching more than one call site's signature.** `layer=3, om=2, mode=3, nv=4`
  is currently unique to `submitPanel`. A second producer adopting it, or the panel being submitted
  by the guest as well as the port, would make the census's "2 of 4264" ambiguous about which is
  which.
- **A live run where `g_Gamestate` is 0 at a frame that still carries the panel.** The claim that
  this is the pause menu rests on the guest's own words, and a route that reaches the panel without
  `GS_PauseMenu` would mean some other stage reuses `0x8001A40C` — which `render_frame.cpp:298`
  already admits it does, for `GS_InventoryMenu` (3) and `GS_OldDragon` (6). `page=0` and
  `captions=5` are what separate them here, and a different page's caption set would separate them
  there.
- **A `func_8001A40C` call trace that re-enters the handler between `0x8001A6C8` and `0x8001A7D8`
  without passing the tiled-quad loop.** That would make `$s4 = 0xE0` live and the colour half of
  this claim wrong. The decompilation's independent `64` argues against it; a trace would close it.
