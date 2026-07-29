---
id: C100
kind: claim
status: holds
created: 2026-07-29
tags: gpu,framework
---

## Claim

Preserving the uploaded VRAM backdrop does NOT leave stale content under 3D gameplay — the design fear behind the black clear does not materialise for this port.

## Evidence

With GameConfig::preserveVramBackdrop=1 (band 1 loads instead of clearing), a gameplay capture still returns a clean frame: 93.2% non-black, 1906 distinct colours, no visible ghosting or stale overlay. The concern that motivated the hardcoded clear — that anything left from the guest's own drawing would be stale — is real for a port whose native renderer owns the frame, but this port's guest redraws the display buffer each frame, so there is nothing stale to show through. NOTE the change does NOT achieve its other goal: the upload-only logo screens still read 1 distinct colour in both buffers (issue 0029).

## What would falsify it

a scene that draws only part of the display area — a small HUD over an otherwise untouched buffer — could reveal stale content that a full-screen gameplay frame hides
