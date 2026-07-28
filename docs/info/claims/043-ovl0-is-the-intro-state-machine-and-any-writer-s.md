---
id: C043
kind: claim
status: holds
created: 2026-07-28
tags: overlay,method
---

## Claim

OVL0 IS the intro state machine — and any writer scan over resident text alone undercounts it

## Evidence

OVL0 makes 35 absolute references to the three intro-state globals, and its recompiled func_8007ABAC (the mode-13 arm) spans the whole machine. Extracted transition table (address -> global <- value): guard[0x80078D94] is set to 2 at 0x8007B0AC, to 0 at 0x8007B900 / 0x8007C7C8 / 0x8007C8A8, and to 1 at 0x8007CA64; sub[0x80078D78] is set to 2 at five sites and to 3 at 0x8007CC20; subsub[0x80078D7C] is driven through 0/1/2/3/4/5 at ~25 sites. The handoff to the resident path is 0x8007CC20 (sub<-3) immediately followed by 0x8007CC24 (subsub<-0). At runtime the guard reads 2 when the resident handler 0x80032B08 runs, so OVL0 reached the handoff from 0x8007B0AC WITHOUT passing any of the four guard-reset sites — which is why 0x80032B08 skips its arena-cursor reset at 0x80032B60. METHOD POINT: a scan of the resident text alone reported ONE writer of that guard, storing 1, which contradicts the observed 2 and would have sent the next step chasing a phantom. Overlay code must be included in any writer scan, and base-pointer (non-lui) addressing is still invisible to both.

## What would falsify it

A run where the guard reads 0 or 1 at 0x80032B08, which would mean one of the reset paths IS taken and the divergence is elsewhere.
