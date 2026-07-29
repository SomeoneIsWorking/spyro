---
id: C124
kind: claim
status: holds
created: 2026-07-29
tags: gpu,milestone,render
---

## Claim

The PC port renders Spyro's attract-mode demo correctly in 3D. A full-VRAM dump at frame 46501 shows a complete game frame (terrain, sky, Spyro, characters, 'DEMO MODE') at VRAM (0,0)-(512,240).

## Evidence

REPL 'vram scratch/screenshots/vram46501.png' at f46501 after the issue-0034 fix; cropped to scratch/screenshots/spyro_gameplay.png. Corroborated by PSXPORT_PRIMDUMP=46501: 1609 polys with sane bboxes (median area 112px, none full-screen), 416 textured, 47 semi-transparent.

## What would falsify it

a VRAM dump at a later odd frame showing no coherent image, or the windowed run looking nothing like the dump — either means the dump was a one-off or is not what gets presented
