---
id: C178
kind: claim
status: holds
created: 2026-08-14
tags: 
depends: game/render/fx_sprite_queue.cpp#project_screen_vertex
---

## Claim

Spyro's native stage-13 screen sprite layer matches the guest producer at semantic timer 171: 551 candidates and 232 accepted faces (25 triangles, 207 quads)

## Evidence

scratch/logs/native-packed-decode-fix.log; shared RTPS phase trace measured 542/542 rows in guest/native and located 271 differing native inputs before the packed signed-shift decode was corrected in game/render/fx_sprite_queue.cpp#project_screen_vertex

## What would falsify it

A same-state isolated guest/native run differs in candidate count, accepted triangle/quad count, ordered packet content, or a reachable screen-space record enters an unowned primitive variant
