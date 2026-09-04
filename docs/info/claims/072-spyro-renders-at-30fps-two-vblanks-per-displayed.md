---
id: C072
kind: claim
status: holds
created: 2026-07-28
tags: gpu,frame
---

## Claim

Spyro renders at 30fps — two vblanks per displayed frame — and the double-buffer flip is verified alive at runtime.

## Evidence

Snapshots at four consecutive frame-boundary ticks (2000..2003) show the current-DRAWENV pointer [0x80075888] holding draw0, draw0, draw1, draw1 — alternating every TWO ticks. The tick counts vblank waits, so the game flips once per two vblanks. That independently explains the title-screen counter advancing 2 per call (C071) and confirms the binary reading of the flip on real data.

## What would falsify it

a scene that flips every tick (60fps) would break the 2:1 relationship; check before assuming 30fps globally rather than for the title screen
