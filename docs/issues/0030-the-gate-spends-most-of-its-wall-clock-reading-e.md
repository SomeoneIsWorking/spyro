---
id: 30
title: The gate spends most of its wall-clock reading every dumped frame
status: open
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
