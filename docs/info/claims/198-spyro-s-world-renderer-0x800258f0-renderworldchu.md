---
id: C198
kind: claim
status: holds
created: 2026-08-16
tags: render,widescreen,re,ownership
---

## Claim

Spyro's world renderer 0x800258F0 (RenderWorldChunks) is now structurally mapped end-to-end: a ~5000-instruction hand-written assembly body (0x800258F0..0x8002A6F4, 0x4E04 bytes) with three phases. Phase 1 (0x800258F0..0x800261A0) walks the environment chunk list (occlusion-group table g_Environment+8 when a0>=0, flat list g_Environment+0/4 when a0<0), culls each chunk by view frustum + distance (MVMVA against g_Camera.m_ViewMatrix at +0x14, radius shift-based thresholds vs g_Environment.m_LodDistance at +0x24 >> 4), and applies the per-chunk environment geometry/color animation (INTPL/DPCS blocks writing into chunk vertex/color arrays). Phase 2 (0x800261A0..0x8002A0B0) projects every surviving chunk's vertices against g_Camera.m_ProjectionMatrix at +0x0 (RTPS) decoding 11/11/10-bit packed deltas from a per-chunk origin, writes SXY2+SZ3 into a scratchpad vertex cache at 0x1F800000 (8-byte interleaved stride), then emits gouraud quads (stride 0x24) and triangles (stride 0x1C) into the packet pool with per-face NCLIP backface culling, a depth/LOD test (sum-of-SZ vs m_CullingDistance/m_LodDistance), whole-face clip-code rejection, and OT binning by average SZ. Phase 3 (0x8002A51C) emits the special-surface (water/lava) textured quads from D_8006D5C8. It publishes the pool pointer to D_800757B0, links g_WorldOT, restores registers from the fixed save area D_80077DD8, and returns.

## Evidence

Read the vendored decomp's asm/renderers/r_environment.s (func_800258F0, SHA-1 of the target SCUS_942.28 is byte-identical to our extraction, so its addresses are ours) end to end, cross-checked against external/spyro-1/include/camera.h (m_ProjectionMatrix@0x0, m_ViewMatrix@0x14, m_Position@0x28) and include/environment.h (m_LodDistance@0x24, m_CullingDistance@0x28, m_OcclusionGroups@0x8). Clip-bound immediates 0x10000/0x1000000/0x2000000 (same as the owned cyclorama 0x8004EBA8) load at 8 sites: 0x80026264, 0x80026844, 0x80027A58, 0x80028174, 0x80028C48, 0x800299A4, 0x8002A2EC, 0x8002A4B8. The decisive depth fact: per-vertex SZ3 is a GTE/scratchpad intermediate used only for LOD and OT binning, and is NEVER written into the emitted GPU packets (they carry SXY + color only) - which is why the interpreted body cannot supply depth and ownership is required.

## What would falsify it

a live census of 0x800258F0's emitted packets showing an SZ word in the packet body, or a mute of 0x800258F0 removing content other than ground/cliffs (C147), or a chunk being emitted with its vertices read from anything other than 11/11/10-bit packed deltas
