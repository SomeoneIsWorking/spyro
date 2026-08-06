---
id: C158
kind: claim
status: holds
created: 2026-08-06
tags: frame,re,gpu,architecture
depends: game/core/frame_loop.cpp
---

## Claim

SPYRO CAN OWN ITS FRAME LOOP, and it must do so in GAME code — the framework's native_step_frame is unreachable here and must not be the vehicle. The guest's main() 0x80012204 is a 15-instruction shell that never returns, but nothing in it is hard to reproduce: two init calls then forever { clear the input-latch flag; call the update 0x8003385C; clamp the elapsed-vblank count to [2,4] as the frame step; open the input-latch flag; restart the count; call the render driver 0x8001ED5C unless [0x8007579C] is set }. The port now runs exactly that loop natively (PSXPORT_SPYRO_FRAME_LOOP=1).

## Evidence

STATIC: disasm of 0x80012204..0x800122A0 on scratch/raw/snap_15000.bin — the loop closes with 'j 0x8001222C' at 0x80012284 and 'bnez v0,0x8001222C' at 0x80012274, and the epilogue at 0x8001228C-0x8001229C has NO branch to it, so main() cannot return. REACHABILITY: native_step_frame is static in native_boot.cpp with exactly two callers, game_main (via native_crt0 <- native_boot_run <- BootStub::run) and dc_step_frame. 'grep -rn BootStub game/' finds ZERO callers in spyro and spider1 and exactly one in Tomba2Engine (game/core/main.cpp:94 'game->stub.run(path)') — the same grep answers both classes, so the method is not blind. RUNTIME: PSXPORT_SELFTEST=startgame, whose whole job is to boot a core and then step it with dc_step_frame, HANGS with the stack _start -> main -> dc_boot_init -> gen_func_80012204 -> gen_func_8001ED5C (scratch/logs/frameown/run_selftest.log) — dc_boot_init never returns, so no harness in this port can reach native_step_frame either. LOOP RUNS: three headless runs of the same binary, scratch/logs/frameown/{A_off,B_on_psx,C_on_native}.log. With the loop OFF, PSXPORT_FNTRACE reports 0x8003385C and 0x8001ED5C first called from ra=0x80012238/0x80012284 (the guest's own call sites, 1271 calls each in 50s). With it ON, the SAME instrument reports ra=DEAD0000 (the port's top-level return sentinel) for both, same first frame 436, 1276 calls each — so the ra field is a discriminator that printed both answers. With the native render branch also on, the port aborts on its first render naming the scene: 'stage selector [0x800757D8] = 13, SPLIT on [0x80078D78]==3, [0x80078D78]=0 selects 0x8007CEE4' (signal 06 = SIGABRT). CADENCE: 28218 presents/vblanks against 15053 calls of 0x8001ED5C over 60s unpaced = 1.85 vblanks per drawn frame, so present must NOT move into this loop 1:1.

## What would falsify it

if a build of spyro ever calls BootStub::run (or otherwise reaches game_main), native_step_frame becomes live and the 'must be game-side' half of this claim needs re-deciding; and if 0x80012204's epilogue ever acquires an inbound branch (a different executable revision), main() could return
