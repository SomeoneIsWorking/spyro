---
id: C144
kind: claim
status: holds
created: 2026-07-30
tags: render,depth,interp
---

## Claim

Tapping native depth DYNAMICALLY in the interpreter roughly doubles coverage where the recompiler's static analysis plateaued: 6.9% -> 13.2% of vertex lookups resolved, per-primitive 2.3% -> 4.2%. The interpreter needs no proof — one tag per GPR records what it currently carries (a projected vertex's Z from mfc2, or a load's source address) and any other write clears it, which is exact where the static tracker had to be conservative. But propagating that tag through the packing arithmetic added only 0.2 points, and the excess of records over lookups (3.6M vs 564K) says why: the depth lands on the vertex CACHE and the copy INTO the packet is the missing link, so further taps are not the lever.

## Evidence

tools/depth_cov.py over three 180s 16:9 runs: baseline (static recompiler taps only) 6.9% per-vertex / 2.3% per-primitive; interpreter mfc2 tap 13.2% / 4.2%; plus provenance propagation through the packing ALU ops 13.4% / 4.2%. Recorded depths went 380587 -> 1247778 -> 3623612 while resolved lookups went 44013 -> 59143 -> 75514. depth_cov.py itself flags the shape: 'a large excess means the port is recording at addresses nothing draws from — a STAGING buffer, not the packet'. 4:3 gate 16/16 with 0 native/substrate divergences, and the 16:9 frame is byte-identical to the pre-tap capture (0 of 164160 pixels), so nothing fabricated a depth that reached the picture.

## What would falsify it

finding the store that assembles a packet from the vertex cache — if depth still fails to reach it after that store is tapped, the cache-to-packet model is wrong, not incomplete
