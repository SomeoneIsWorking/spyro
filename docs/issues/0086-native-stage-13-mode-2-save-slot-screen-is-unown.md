---
id: 86
title: Native stage-13 mode 2 save-slot screen is unowned
status: open
symptom: After the verified native memory-card menu opens card.mcr, the title overlay advances to mode 2 and SpyroRenderer::titleMenuRender returns false; renderScene intentionally aborts at the first unowned presentation boundary.
tags: render,native,title,save,boot-to-play,re
state_items: S003
created: 2026-08-26
updated: 2026-08-26
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
