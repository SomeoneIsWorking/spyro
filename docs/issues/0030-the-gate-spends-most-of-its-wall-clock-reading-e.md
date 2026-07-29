---
id: 30
title: The gate spends most of its wall-clock reading every dumped frame
status: resolved
symptom: tools/gate.sh takes several minutes, most of it after the 40s run — the distinct-occupancy check opens and scans all ~15000 PPM dumps (~5 GB)
tags: gate,workflow
created: 2026-07-29
updated: 2026-07-29
---

The 'distinct frame occupancies' check exists for a good reason: frame COUNT alone cannot tell 'the boot advances through content' from 'one screen is being re-presented', and it was 218 for a held splash. But it currently reads EVERY dumped frame, which is ~15000 files and several GB per run, and that dominates the gate's runtime — the port itself only runs for 40 seconds.

Under external machine load this pushed gate runs past the point where they were being killed by timeouts, which cost several iterations of this session.

LIKELY FIX: sample rather than exhaustively scan. Distinctness over a few hundred evenly spaced frames answers the same question — 'does the picture change over the run' — at 2% of the I/O. Keep the full scan available behind a flag for when a specific frame range is under investigation.

WORTH CHECKING FIRST whether PSXPORT_GPU_DUMP needs to write every frame at all. The gate's other checks do not use the dumps, and the dump itself is not free either.

DO NOT edit tools/gate.sh while a gate is running — bash reads a script incrementally and an in-place edit can make a running gate execute spliced garbage (recorded in issue 0026).

### Resolution (2026-07-29)
FIXED, and the cost was bigger than the gate's own runtime.

PSXPORT_GPU_DUMP now accepts dir[:every] (psxport) and the gate uses ':20'. Frames-presented comes from the LOG rather than a file count, since counting sampled files would divide the real number by the interval and silently move the threshold.

MEASURED: the same 40s run goes from 19003 presented frames to 54151 with the dump sampled — the instrumentation was costing the port about 2.8x its speed. Dump volume drops from 6.6 GB / 19003 files to 963 MB / 2708. So this was never only about the post-run scan; writing a PPM per frame was the dominant cost INSIDE the measured window.

CONSEQUENCE worth carrying forward: every frames-presented figure recorded earlier in this session (16508, 18586, 18809, 19003) was measured on a port carrying that I/O, and is not comparable with anything measured after this change. The relative comparisons between those numbers still stand — they were all taken the same way — but the absolute figures are not the port's real throughput.

A BUG CAUGHT WHILE MAKING THIS: my first version skipped the dump with an early , which would also have skipped frame_finalize() at the end of the same function — the depth-table reset, batch reset and s_frame++. The frame counter would have stopped advancing and geometry batches would have accumulated across frames. Cheap to write, expensive to diagnose; the fix skips only the write.
