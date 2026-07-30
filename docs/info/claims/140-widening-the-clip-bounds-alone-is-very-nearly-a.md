---
id: C140
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen
---

## Claim

Widening the clip bounds ALONE is very nearly a no-op: it changed 5 pixels of a 684x240 frame. The guest rejects a face only when all three vertices share an off-screen side, so with the projection still centred on 256 almost nothing lies WHOLLY beyond 512. The two halves of widescreen are not independent — OFX at nw/2 is what brings wholly-off-left faces into view, and the widened right bound is what stops the faces it pushes past 512 from being rejected in exchange.

## Evidence

Two 684x240 captures of frame 46501 differing only in whether the eleven bound sites were patched: scratch/screenshots/wide684.png vs wide_bounds2.png. Per-pixel diff over the whole frame: 5 differing pixels, all at x>=640. The patch is proven to have fired by the per-renderer log line reporting the before/after instruction word (3C190200 -> 3C1902AC etc.).

## What would falsify it

a scene with geometry that IS wholly beyond the 4:3 right edge — a wall or object filling the right margin would make the bound widening visible on its own
