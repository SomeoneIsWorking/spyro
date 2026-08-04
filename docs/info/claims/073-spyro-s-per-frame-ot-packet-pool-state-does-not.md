---
id: C073
kind: claim
status: holds
created: 2026-07-28
tags: frame,framework
reconfirmed: 2026-08-04
verified_at: 2026-08-04
depends: external/psxport/runtime/recomp/native_boot.cpp#native_step_frame
---

## Claim

Spyro's per-frame OT/packet-pool state does NOT match psxport's per-frame GameConfig model, so those fields must stay 0 rather than be filled with plausible addresses.

## Evidence

Read from a live port two ways that agree (a frame-boundary snapshot and the REPL): [0x800785E8]=0x80187BB0 and [0x800785EC]=0x801A3BB0 are the per-parity OT regions (stride 0x1C000), [0x800785F0]=0x801BFBB0 the shared pool, [0x800785F4]=0x801BFBB8. But the framework's native_step_frame model (native_boot.cpp:111-116) assumes ONE otBasePtr global rewritten each frame with otRegionBase + parity*otRegionStride, plus poolPtrCur/poolPtrLast. Spyro instead keeps per-parity OT and pool pointers INSIDE the two draw envs (+112/+116/+120, initialised at 0x8005B750 from those globals) and selects between them via the current-DRAWENV pointer [0x80075888]. Same concept, different shape — filling otBasePtr with a Spyro address would write the wrong global every frame.

## What would falsify it

if the framework's per-frame group is generalised to a per-parity-selector model, or Spyro turns out to also mirror the OT base into a single global, the fields could be filled

## Re-confirmed 2026-08-04

STILL HOLDS, with one LABEL CORRECTED by the C151 decompile — the conclusion (leave psxport's per-frame GameConfig fields at 0) is untouched, but the wording above is imprecise and would mislead the next reader. [0x800785E8]=0x80187BB0 and [0x800785EC]=0x801A3BB0 are NOT 'the per-parity OT regions'; they are the per-parity PACKET-POOL BASES — 0x8005B6F8 initialises them as env0+0x70 and env1+0x70, two contiguous 0x1C000 regions — and each parity's OT is derived at frame time as that base + 0x1C000 (0x8001ED5C). Confirmed numerically on a gameplay snapshot (snap_15000): env0's +0x70 is 0x80187BB0 and the live OT pointers are 0x801A3BB0 = 0x80187BB0+0x1C000. The 0x1C000 stride this claim measured is therefore the REGION SIZE, which is why it reads as a per-parity stride. [0x800785F0]=0x801BFBB0 and [0x800785F4]=0x801BFBB8 are confirmed SHARED between the two envs, exactly as this claim says: 0x8005B6F8 assigns env1's +0x74/+0x78 from env0's.
