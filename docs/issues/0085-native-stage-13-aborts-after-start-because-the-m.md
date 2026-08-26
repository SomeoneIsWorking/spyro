---
id: 85
title: Native stage-13 aborts after START because the memory-card menu sprite recipe is unowned
status: resolved
symptom: The native Spyro 1 title renderer handles mode 0 but titleMenuRender returns false when START advances 0x80078D78 to mode 1, so the boot-to-play path aborts before the memory-card menu can be presented.
tags: render,native,title,memcard,boot-to-play,re
state_items: S002
created: 2026-08-26
updated: 2026-08-26
---

## Root cause

The shipping stage-13 seam already owns the common actor/world/cyclorama tail and the screen-space sprite emitter 0x8007CD38, but its dispatcher only transcribed mode 0 of overlay function 0x8007CEE4. Mode 1 uses the same emitter and backdrop; the missing unit is its finite menu-state-to-sprite-command recipe, not another renderer or a guest fallback.

## Ground truth

`scratch/decomp/title_stage13.c` decompiles the resident OV_5B800 function, and the vendored Rosetta source `external/spyro-1/src/overlays/titlescreen.c` `TitlescreenDraw` independently corroborates the same mode-1 switch: two border commands plus substates 0..13 and 15, with no case 14. State layout comes from `g_TitlescreenState`: mode 0x80078D78, sub-tick 0x80078D84, substate 0x80078D88, option 0x80078D8C, and selected card 0x80078DA0.

## Current implementation boundary

`game/render/title_menu_recipe.*` is the pure bounded recipe owner; `title_menu_state.*` is the one guest-memory lens shared by native presentation and diagnostics; `fx_title_menu.cpp` sends each command through the existing shipping `spriteEmit`, preserving the one native submitter and ProducerScope identity. Hermetic tests cover every switch value 0..15, the missing/default arm, option timing threshold, blink polarity, both signed card-slot indexing cases, and exact/mutated/truncated command-stream comparison.

`title_menu_oracle.cpp` is repo-local because the boundary being checked is game-specific: it claims OV_5B800's generated override slots for `0x8007CEE4` and its nested `0x8007CD38` only when `PSXPORT_TITLE_MENU_ORACLE=1`. On the reference leg it runs both retained generated bodies, captures the emitter's actual `a0..a3` stream, and compares that ordered stream to the shipping recipe. No shared psxport mechanism knows the title overlay, its state addresses, or this recipe, so moving the comparator upstream would put game facts in the framework. The comparator's hermetic negative controls reject a changed command and a truncated stream.

## Real-data verification

The serialized reference run used the exact retained-body comparator:

```sh
env PSXPORT_SPYRO_DISC=/path/to/spyro.chd PSXPORT_ASSET_DIR=external/psxport PSXPORT_RENDER_PATH=gte PSXPORT_FORCE_BUTTONS=FFF7 PSXPORT_TITLE_MENU_ORACLE=1 PSXPORT_NATIVE_FRAMES=3000 PSXPORT_NOPACE=1 PSXPORT_NOAUDIO=1 PSXPORT_DEBUG=scene,titleoracle scratch/bin/spyro_port scratch/bin/spyro/SCUS_942.28
```

At framework pin `99a42aa3`, `scratch/logs/title-menu-reference-99a42aa3.log` records 35
`[titleoracle] PASS` results across reached substates 0, 15, and 1, zero
`DIVERGES|REFUSED|STUCK|FATAL` matches, and a clean exit at the 3,000-present cap. This is the exact
guest-argument comparison; FNTRACE is not the right instrument because this native picture recipe
does not replace a whole guest function, and NDIFF compares machine state rather than the ordered
presentation commands this ownership slice emits.

```sh
env PSXPORT_SPYRO_DISC=/path/to/spyro.chd PSXPORT_ASSET_DIR=external/psxport PSXPORT_RENDER_PATH=native PSXPORT_FORCE_BUTTONS=FFF7 PSXPORT_NATIVE_FRAMES=3000 PSXPORT_NOPACE=1 PSXPORT_NOAUDIO=1 PSXPORT_DEBUG=scene,titlefx scratch/bin/spyro_port scratch/bin/spyro/SCUS_942.28
```

At the same pin, `scratch/logs/title-menu-native-99a42aa3.log` identifies build
`d4de2a1-dirty+psxport-99a42aa3`, reaches mode 1, and prints five consecutive
`mode=1 substate=0 anim=0..4 recipe=3 emitted=3` results. It then opens
`scratch/saves/card.mcr` and ends with watchdog signal 06 and libc `abort`. The operator wrapper
reported exit 139, but the preceding debugger reproduction falsified the segfault diagnosis:
SIGABRT comes from `SpyroRenderer::drawFrame` through `abortNotImplemented`, after mode advances to
2 and `titleMenuRender` deliberately returns false at its explicit unowned-mode refusal.

The same native run wrote `present_50.ppm`, `present_150.ppm`, and `present_250.ppm`; their logged
non-black coverage is 4.08%, 93.26%, and 93.33%. Visual inspection of
`scratch/screenshots/title-menu-native-99a42aa3-montage.png` shows a coherent Universal logo,
Insomniac mountain scene, and Spyro title scene. These gitignored captures supplement the exact
runtime and comparator record rather than replacing it.

### Resolution (2026-08-26)
At framework pin `99a42aa3`, the retained-body comparator passed 35 reached mode-1 calls with zero
bad-status matches and the native leg emitted five correct substate-0 recipes before opening
card.mcr. Its exact log ends in signal 06 / `abort`, and GDB proved that stop is the explicit mode-2
SIGABRT refusal, not a segfault or mode-1 defect. The exact comparator remains in place; mode 2 is
split to issue 0086 and S003 rather than weakening or bypassing the refusal.
