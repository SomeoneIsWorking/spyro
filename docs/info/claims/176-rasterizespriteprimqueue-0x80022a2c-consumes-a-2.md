---
id: C176
kind: claim
status: holds
created: 2026-08-13
tags: render,native-producer,sprite-queue
depends: game/core/native_render.cpp#census_sprite_queue
---

## Claim

RasterizeSpritePrimQueue 0x80022A2C consumes a 256-entry null-terminated actor queue and all four primitive-stream variants are live: bit11, bit01, Gouraud quad, and Gouraud triangle. A 7000-present reference run observed 1252 calls, 84976 queued records, 28 mesh indices, and 1455901 valid primitives; zero invalid actor, mesh, stream, or vertex-index reads. Null mesh entries and the all-ones mesh-0 sentinel occur before the guest visibility branch and are not drawable mesh inputs.

## Evidence

scratch/logs/spriteq-census-evidence-final.log; game/core/native_render.cpp read-only PSXPORT_SPRITE_QUEUE_CENSUS scanner; decompile scratch/decomp/world_renderer_80022a2c.c

## What would falsify it

A reference capture reaches a fifth primitive encoding, a non-null queue past 256 entries, or the scanner reports any invalid actor/mesh/stream/vertex-index after sentinel classification
