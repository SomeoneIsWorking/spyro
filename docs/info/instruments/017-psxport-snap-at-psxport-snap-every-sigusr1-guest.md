---
id: I017
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

PSXPORT_SNAP_AT / PSXPORT_SNAP_EVERY / SIGUSR1 — guest RAM capture from a RUNNING port (snapshot.cpp)

## Validated by

Shown to capture DIFFERENT state at different times rather than a fixed image: snapshots at ticks 2000-2003 show the current-DRAWENV pointer alternating draw0/draw0/draw1/draw1, which is how the 30fps flip cadence was established (C072). Cross-checked against a completely independent reader — the REPL's 'r 0x800785E8' on a live port returns the same four words as the snapshot file. Each capture writes a .txt sidecar naming the frame and reason, so a directory of 2 MB dumps stays self-describing, and it refuses past PSXPORT_SNAP_MAX with a warning rather than silently skipping.

## Known failure modes

(none recorded yet)
