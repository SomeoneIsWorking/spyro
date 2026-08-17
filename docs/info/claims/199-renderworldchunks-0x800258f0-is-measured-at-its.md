---
id: C199
kind: claim
status: holds
created: 2026-08-17
tags: render,widescreen,re,ownership
---

## Claim

RenderWorldChunks 0x800258F0 is measured at its game-state input boundary on a live run: PSXPORT_WORLD_CENSUS (native_render.cpp) armed an override on the renderer, read g_Environment's chunk lists + the packet pool before/after at every call, and redispatched the unchanged guest body. A reference-leg run of 5000 presented frames made 2088 calls — 1555 via the flat sector list (a0<0, from the render driver 0x8001EA6C; 169,495 entries, max 109/call) and 533 via occlusion-group lists (a0>=0, from CreateEnvironment 0x8002B9CC; 72,879 byte-indexed entries, max 178/call) — and moved 40,684,452 bytes through the packet pool with zero empty calls. The two list types are structurally different: the flat list is length-counted (g_Environment.m_SectorCount at +0x4), the occlusion-group list is 0xFF-terminated byte indices into an object table — an oracle must not conflate them.

## Evidence

scratch/logs/world_census2.log: '[worldcensus] CENSUS: calls=2088 (a0<0 flat-list=1555, a0>=0 occlusion-group=533) flat_entries=169495 (max 109) occ_entries=72879 (max 178) pool_bytes=40684452 pool_bytes_zero_calls=0 last_occlusion_group=44'. The read addresses come from C198 (g_Environment 0x800785A8: +0x0 m_SectorPointer, +0x4 m_SectorCount, +0x8 m_OcclusionGroups, +0xC m_OcclusionGroupCount) and the a0 values from tools/callsite_args.py (0x8001EA6C passes -1, 0x8002BA50 computes it). The census is gated behind PSXPORT_WORLD_CENSUS=1 and is diagnostic-only — it installs nothing on a normal run.

## What would falsify it

a census run reaching the field (stage 0) that reports zero calls, or a call whose flat-list entries disagree with g_Environment.m_SectorCount, or an occlusion-group walk that does not terminate on 0xFF
