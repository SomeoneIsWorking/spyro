---
id: C060
kind: claim
status: holds
created: 2026-07-28
tags: milestone,recomp
---

## Claim

THE PORT NO LONGER CRASHES — gate 11/11, zero recomp misses, frame count scales with wall time

## Evidence

Verified against the trap that fooled me earlier this session: a fixed frame count can be a CRASH POINT rather than a duration. It is not. 20s -> 8468 frames, 60s -> 24366 frames, both exiting rc=137 (killed by the timeout, still running). Roughly 420 fps headless. Full gate: port still running at timeout PASS, 13118 frames, 19 distinct occupancies, 1640 late prim-submitting frames, 7 CD loader invocations, 4,945,920 bytes loaded, 26 CD completions, ZERO recomp misses, zero refused HLE registrations, 2 overlay identifications in slot 0, ledger self-consistent. The secondary numbers moved too: loader calls 6 -> 7, bytes 3,686,400 -> 4,945,920, and overlay identifications 1 -> 2, meaning overlays are now being SWAPPED at runtime rather than one being loaded once. The last fix was the no-index computed jump:  where the register holds a plain IMMEDIATE with no base+index to decompose, so the targets are simply the constants assigned to it (0x800243FC reaches 0x800240F8 or 0x800241A8).

## What would falsify it

A run where the frame count stops scaling with the timeout, or any rc other than 137 at any duration.
