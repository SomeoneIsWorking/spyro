---
id: C151
kind: claim
status: holds
created: 2026-08-04
tags: frame,re,gpu
depends: external/psxport/tools/decomp.sh
reconfirmed: 2026-08-04
verified_at: 2026-08-04
---

## Claim

Spyro's per-frame OT/packet-pool RESET and its DrawOTag call site are both inside 0x8001ED5C, and the three libgpu entries involved are identified from the game's OWN debug strings: DrawOTag=0x8005FD64, PutDrawEnv=0x8005FDD8, PutDispEnv=0x80060030. The reset is: select the OTHER draw env (env0=0x80076EE0 / env1=0x80076F64) by comparing the current-env pointer [0x80075888]; reload three working pointers from that env's +0x70/+0x74/+0x78 fields into [0x800757B0] (packet pool), [0x80075820] and [0x8007581C]; derive the OT pointers [0x800756FC]=[0x80075710]=[0x80075780] as pool+0x1C000; zero [0x800758B0]; then commit [0x80075888]. The frame ends with PutDispEnv(env+0x5C), PutDrawEnv(env), and DrawOTag(FUN_80016784(0x800)) where 0x80016784 is the game's own OT collapse/terminate helper returning the list head.

## Evidence

Ghidra headless via external/psxport/tools/decomp.sh on scratch/raw/snap_470.bin (project spyro470); decompiled bodies in scratch/decomp/frameloop.c and scratch/decomp/libgpu_drawotag.c. The libgpu identifications are NOT inferred: each body passes a literal to the libgpu trace hook, and those literals were read back out of the RAM dump directly — 0x800118FC='DrawOTag(%08x)...' (referenced by 0x8005FD64), 0x80011910='PutDrawEnv(%08x)...' (0x8005FDD8), 0x80011944='PutDispEnv(%08x)...' (0x80060030). Independently corroborated at runtime by C150's watchpoint data: gen_func_8001ED5C is the innermost writer of the pool pointer 0x800757B0 ~2x per frame (12526 stores over ~6000 frames) and is the renderers' caller (chain 8001F798 <- 8001ED5C <- 80012204). The stride 0x1C000 matches the per-parity OT stride C073 measured independently from a live snapshot.

## What would falsify it

if a decompile of 0x8001ED5C on a GAMEPLAY-state dump (this one is snap_470, a boot/logo frame — MAIN text is always resident so the code is the same, but the stage selector [0x800757D8] takes a different arm) shows the stage-0 arm is not the one that runs during gameplay, the reset sequence above may not be the live one

## Re-confirmed 2026-08-04

FALSIFIER DISCHARGED on real gameplay data. PSXPORT_SNAP_AT=15000 (a gameplay frame; the boot/logo phase ends by ~f600) gives scratch/raw/snap_15000.bin, and the stage selector [0x800757D8]=0 there — so the stage-0 arm, the one holding the renderers and the DrawOTag call, IS the arm that runs during gameplay, not just on the logo frame the decompile was taken from. Note also that the RESET sequence sits ABOVE the stage switch and runs unconditionally, so it was never stage-dependent. THE ARITHMETIC CHECKS OUT AT RUNTIME: current env [0x80075888]=0x80076EE0 (env0), its +0x70=0x80187BB0, and the three OT pointers [0x800756FC]=[0x80075710]=[0x80075780]=0x801A3BB0 = 0x80187BB0+0x1C000 EXACTLY, which is the derivation this claim transcribes. The live pool pointer [0x800757B0]=0x80192320 has bumped 0xAB00 bytes above the env's base mid-frame, which is why it does not itself equal the OT minus 0x1C000 — that is the bump allocator working, and it is the reason a naive equality check on a live sample would have looked like a contradiction. CAVEAT, stated rather than papered over: that 0x800756FC/0x80075710/0x80075780 are the ORDERING TABLE root (as opposed to the top of the region the OT is indexed downward from) is NOT proven here — the pool+0x1C000 derivation is transcribed from the instruction stream and confirmed numerically, but the ROLE is inherited from C073's labelling, not independently established.
