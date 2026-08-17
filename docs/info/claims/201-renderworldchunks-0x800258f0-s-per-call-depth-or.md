---
id: C201
kind: claim
status: holds
created: 2026-08-17
tags: render,widescreen,depth,oracle
---

## Claim

RenderWorldChunks 0x800258F0's per-call depth oracle is live (PSXPORT_WORLD_CENSUS=1) and shows the GROUND is genuinely 3D: per-call SZ3 depth range 4..12806 (mean 7805) from the scratchpad SZ3 array at 0x1F8002AC, and 358,830 faces across 802 OT bins (depth>>7, bins 2..806) with a smooth near-to-far gradient (54668 faces in bins 0x00..0x3F falling to 141 in 0x300..0x33F, zero beyond). The pool emit is 688,938 packets (329,840 quads + 359,098 tris) with 0 malformed and 0 unparsed bytes — a clean packet stream whose packets carry NO depth word (SZ3 is a scratchpad-only intermediate, confirming C198's structural claim at runtime). The OT bin for a face is its average SZ >> 7, so the bin distribution IS the quantized depth distribution — the exact signal 2D-vs-3D discrimination (widescreen re-centering + 60fps interpolation) needs.

## Evidence

scratch/logs/world_oracle6.log: ORACLE line (sz3_range 4..12806 mean 7805), ORACLE-OT line (walks=1532 bins_used=802 faces=358830 nearest_bin=2 farthest_bin=806 near=1240 far=0) and the ORACLE-OT band histogram. The SZ3 array base 0x1F8002AC and 2-byte stride come from r_environment.s (lui s7,0x1F800004; addi t8,s7,0x2A8; mfc2 v1,C2_SZ3; sh v1,0(t8); t8+=2); the vertex count for a chunk is (lw(chunk+0x14))&0xFF; the OT bin is srl t2,t2,7 (0x80026DBC) into g_WorldOT=0x80075820 (0x800 bins x 8 bytes). The scratchpad was initially read through ram_range() which silently rejected 0x1F8002AC (out of guest-RAM range) — the fix was to let Core::mem_r16 handle the scratchpad alias (mem.cpp:122), a silent-zero trap worth recording.

## What would falsify it

a widescreen-observable case (e.g. a field with a wide flat plane) whose SZ3 range collapses to a single value (proving the ground is not depth-spread), or a capture showing the ground's packets carrying a depth word in their body
