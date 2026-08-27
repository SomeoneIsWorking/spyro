---
id: 86
title: Native stage-13 mode 2 save-slot screen is unowned
status: resolved
symptom: After the verified native memory-card menu opens card.mcr, the title overlay advances to mode 2 and SpyroRenderer::titleMenuRender returns false; renderScene intentionally aborts at the first unowned presentation boundary.
tags: render,native,title,save,boot-to-play,re
created: 2026-08-26
updated: 2026-08-27
state_items: S003
---

## Grounded boundary

At framework pin `99a42aa3`, `scratch/logs/title-menu-native-99a42aa3.log` identifies build
`d4de2a1-dirty+psxport-99a42aa3`, reaches five correct mode-1 `recipe=3 emitted=3` frames, opens
`scratch/saves/card.mcr`, then ends with watchdog signal 06 and libc `abort`. The preceding debugger
run locates that SIGABRT in `SpyroRenderer::drawFrame` through `abortNotImplemented`: it is the
explicit refusal from `game/render/fx_title_menu.cpp` when title mode `[0x80078D78]` becomes 2, not a
segfault and not a mode-1 recipe failure. The operator wrapper's exit-139 report is therefore not the
root-cause classification.

## Required work

RE and own `0x8007CEE4` mode 2, the three-slot save-screen presentation, through the existing
title-menu state/recipe/sprite-emitter owners. Retain a generated-body command-stream comparator and
prove the reached mode-2 states before enabling submission. Do not replace the refusal with guest
fallback or an empty-success path.

## State link

Affects S003.

## Implemented boundary

The mode-2 arm is transcribed from `scratch/decomp/title_stage13.c` and corroborated by the
byte-matching Rosetta source. `title_menu_state` is still the only owner of the overlay addresses and
save-file layout; it summarizes three guest slots plus the slide-table value. `title_menu_recipe`
maps those summaries to a bounded stream with an exact 18-command maximum. The shipping renderer
uses the same emission loop for modes 1 and 2, and the retained-body oracle now selects the matching
recipe for either mode. Generated code remains untouched and there is no guest fallback.

The first live run exposed a separate shared-state collision: `[0x80078D7C] == 2` is paired-actor
state only when title mode is 3, but is also the overwrite state in mode 2. `render_frame` now owns
that predicate once and qualifies it by mode 3. The next run exposed that the title scheduler treated
a guest render-suppressed update as complete with zero delivered fields. The driver now presents the
previous picture for one host-owned field through `FieldScheduler`; the cadence predicate no longer
contains a zero-field suppression exception.

## Real product evidence

At framework `3a8256e9`, the serialized real-disc run
`scratch/logs/spyro-mode2-native-wide-lerp-3a8256e9.log` used the native renderer, forced 16:9, and
enabled interpolation. It reached mode-2 state 4 for 174 draws, emitted `recipe=8 emitted=8` on every
recorded call, exited cleanly at 900 presented fields, and reported the frame-loop contract
satisfied across 415 logic frames. It announced `aspect=1`, `wide_engine=1`, `native_width=512`, and
`render_width=684`; neither the guest-VSync trap nor a renderer refusal fired.

`scratch/screenshots/spyro-mode2-wide-lerp-present-380.png` is the inspected present-stage image:
960x720, 69.72% non-black, with three EMPTY slots, New Game/Load Game, the card footer,
Spyro, and the widened mountain scene all coherent. The retained reference path is intentionally
fail-fast at guest VSync under the native-frame ownership contract, so this resolution does not
claim a live generated-body comparison for mode 2; the comparator remains installed for the future
diagnostic tail split, while the 26 hermetic recipe cases exercise every state and reject mutated and
truncated streams.

C227 records this claim with its runtime falsifier and code dependencies.

### Resolution (2026-08-27)
Owned binary-defined mode 2 in the existing title state/recipe/oracle/sprite-emitter path. The first
live run also exposed and fixed a paired-actor state collision and a render-suppressed zero-fence
step. A real-disc native 16:9+lerp run reached mode-2 state 4 for 174 draws, emitted all eight
commands per frame, captured the complete three-slot UI, satisfied the native frame contract, and
reached the 900-field cap without guest VSync or renderer abort.
