---
id: C127
kind: claim
status: falsified
created: 2026-07-29
tags: gpu,widescreen,re
falsified_on: 2026-07-30
---

## Claim

Spyro's GTE projects ~25% of vertices outside the visible 512-wide frame (16.9% at sx<0, 7.9% at sx>=512), so a widened view has real geometry to show — but the terrain renderer trivially rejects faces using clip bounds that are IMMEDIATE constants in guest code (right bound 512<<16 at 0x8004ED8C).

## Evidence

PSXPORT_DEBUG=sxhist over a level run, f104500: n=3248364 verts, below0=548706 (16.9%), atOrAbove320=683934; bucketed, [512,inf) = 257142 (7.9%) and [0,512) = 2442516 (75.2%). Clip bounds read from the disassembly at 0x8004ED84-8C: lui t5,0x0001 / lui t6,0x0100 / lui t7,0x0200, tested at 0x8004EE0C-EE3C as sy<=0, sy>=256, sx<=0, sx>=512 with the codes ANDed across a face's 3 vertices.

## What would falsify it

a scene whose sxhist shows almost nothing outside [0,512) — that would mean this game culls before projecting and widescreen needs frustum work, not just clip bounds

## FALSIFIED 2026-07-30

THE COUNT WAS WRONG — 7 of 19, not 8, and the sizes I implied were unusable. I measured the clip-bound immediates over spans that ran to the next FAMILY MEMBER rather than the next FUNCTION, which over-attributed bounds to whichever member happened to precede a gap. The projection measurement in that claim (~25% of vertices outside the 512-wide frame) STANDS and is unaffected; it is the family accounting that was sloppy.

Re-measured against real function extents from generated/rec_decls.h — the seven members that actually contain lui rX,0x0200:
    0x8004EBA8    278 instr   x1   <- OWNED, verified 400/400 (C130), widened (C131)
    0x800580F4    244 instr   x1   active, identity-probed 40/40 — but NOT terrain-shaped (one RTPS,
                                   no clip-code packing, no scratchpad cache), so its 512 bound is
                                   used some other way; read it before assuming
    0x8004F000    303 instr   x1   never called in the measured scene
    0x80022A2C    598 instr   x1   active, identity-probed 40/40
    0x8001F798   1113 instr   x1
    0x80020F34   1726 instr   x1
    0x800258F0   4995 instr  x14   the hottest guest function; also the biggest depth-coverage gap (0038)

Replaced by C132. Superseded for anyone sizing the widescreen work: ~9000 instructions remain across six renderers, not a vague 'family'.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
