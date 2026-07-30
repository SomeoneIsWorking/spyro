---
id: C147
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,ownership
---

## Claim

MUTE MAP, corrected and now agreeing with the decomp's names on every entry: 0x8004EBA8 EmitStaticActorMeshList draws the sky and distant terrain; 0x800258F0 RenderWorldChunks draws the GROUND and cliffs; 0x8001F798 EmitActorDrawList draws the orange character; 0x80020F34 EmitSecondaryActorPrimitives draws a SECOND character; 0x80022A2C RasterizeSpritePrimQueue draws the foreground gem and the DEMO MODE caption (both sprites); 0x8004F000 EmitStaticActorMeshListFogged never runs in this level because it is the fogged variant. Together they account for every visible element of the frame.

## Evidence

Five captures of frame 46501 with PSXPORT_MUTE_FN, each preceded by deleting scratch/screenshots/f46501.png so a failed run cannot silently reuse the previous image: recheck_800258F0.png, recheck_80020F34.png, recheck_8001F798.png against mute_none.png. Independently corroborated by open-spyro's symbol names (external/open-spyro/config/spyro.main.ld), which were NOT used to derive the map — they were what prompted the re-check by disagreeing with it.

## What would falsify it

a different level or scene where a muted body removes different content — this is one frame of one level, and 0x8004F000 (the fogged variant) never runs here at all
