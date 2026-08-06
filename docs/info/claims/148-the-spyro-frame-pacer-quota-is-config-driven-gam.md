---
id: C148
kind: claim
status: holds
created: 2026-08-03
tags: pacing
reconfirmed: 2026-08-06
verified_at: 2026-08-06
depends: game/core/vsync.cpp
---

## Claim

The Spyro frame pacer quota is config-driven (GameConfig::paceQuota=1), not the legacy Tomba2 scratchpad byte; windowed vblank timebase runs at the true 60Hz and the 3D scene displays ~30fps

## Evidence

gdb store-watchpoint caught gen_func_8001F798 (generated/shard_4.c:3010) writing vertex data over the old quota byte 0x1F800235 (values 7..38); post-fix windowed 45s run = 2673 vblanks (~59.4/s = 60Hz, display 30fps per C072 2-vblank flip) vs pre-fix ~2fps in-scene; gate 16/16 headless unchanged

## What would falsify it

if a windowed run shows in-scene vblank cadence dropping well below 50/s, or headless gate frame counts regress

## Re-confirmed 2026-08-06

RE-VERIFIED 2026-08-06 against spider1's paceQuota defect (that port's quota was 2 against a 1-vblank cadence). Spyro's cadence re-derived from its OWN call site, not copied: game/core/vsync.cpp has the ONLY gpu_pace_frame call in game/ (grep: 1 hit), inside vblank_wait's while loop, which also calls gpu_present and advances the counter by exactly 1 per iteration -- so pace calls == presents == vblanks 1:1 by construction and the quantum IS one vblank. MEASURED windowed (headless is never paced) with the new game-side PSXPORT_DEBUG=pace channel over a 32.98 s steady window: 1979 vblanks / 1979 pace entries / 1979 presents = 60.00/s each, pace-per-vblank 1.0000. NEGATIVE CONTROL on the same instrument, same window, quota rebuilt to 2u: 30.00 vbl/s, 30.00 pace/s, 30.00 presents/s -- the whole game clock halves (in Spyro the vblank counter is advanced BY this loop, so a wrong quota slows the game uniformly rather than desyncing presents from logic as in spider1). Instrument-off control (PSXPORT_DEBUG=presentskip only, timestamped): 60.00 presents/s, identical, so the instrument does not move the measurement. Logs scratch/logs/pace/{A_quota1,B_quota2_control,C_quota1_no_pace_instrument}.log
