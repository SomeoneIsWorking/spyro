---
id: I043
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

PSXPORT_DEBUG=pace — game-side gpu_pace_frame / present / vblank cadence counters (game/core/vsync.cpp), the check on GameConfig::paceQuota

## Validated by

VALIDATED BY RUNNING IT AGAINST BOTH CLASSES, not by reasoning. Positive (the defect present): a build with paceQuota=2u printed 30.00 vbl/s, 30.00 pace/s, 30.00 presents/s over a 33 s window -- the failing answer, half the field rate. Negative (the defect absent, shipped value 1u): 60.00/60.00/60.00 over the same window, pace-per-vblank 1.0000. Third leg with the channel OFF (PSXPORT_DEBUG=presentskip only, wall-timestamped): 60.00 presents/s, so the instrument does not perturb what it measures. Counters are incremented UNCONDITIONALLY on the same lines as the events, so they carry their denominator whether or not anyone is watching; the log line carries its own monotonic t= so rates come from the log alone. WHAT IT CANNOT SEE: pace calls from anywhere else (bounded by grep: only native_boot/native_stub, which this port never runs, and fps60, unreachable here); presents made by the framework outside this loop; and the DRAW rate -- the DMA2 OT walk drains the RenderQueue before this boundary (gpu_native.cpp rq.flush at the end of gpu_dma2_linked_list), measured rq_unconsumed=0 of 1979 iterations, so use presentskip's rebuild_geom for that. Analyzer scratch/logs/pace/analyze.py REFUSES to print rates when a channel contributes <2 samples in the window (exit 2), verified on an empty window.

## Known failure modes

(none recorded yet)
