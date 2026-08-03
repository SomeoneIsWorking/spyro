---
id: C148
kind: claim
status: holds
created: 2026-08-03
tags: pacing
---

## Claim

The Spyro frame pacer quota is config-driven (GameConfig::paceQuota=1), not the legacy Tomba2 scratchpad byte; windowed vblank timebase runs at the true 60Hz and the 3D scene displays ~30fps

## Evidence

gdb store-watchpoint caught gen_func_8001F798 (generated/shard_4.c:3010) writing vertex data over the old quota byte 0x1F800235 (values 7..38); post-fix windowed 45s run = 2673 vblanks (~59.4/s = 60Hz, display 30fps per C072 2-vblank flip) vs pre-fix ~2fps in-scene; gate 16/16 headless unchanged

## What would falsify it

if a windowed run shows in-scene vblank cadence dropping well below 50/s, or headless gate frame counts regress
