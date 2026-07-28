---
id: C040
kind: claim
status: holds
created: 2026-07-28
tags: overlay,blocker,input
---

## Claim

The level overlay never loads because the stage mode [0x800757D8] is stuck at 13 and the load only runs at mode 4 or 5

## Evidence

main calls the stage dispatcher 0x8003385C unconditionally at 0x80012230, and that function's whole body is a switch on the global [0x800757D8]. Decoded arms: 1->0x8002DF9C, 2->0x8002E12C, 3->0x8002EB2C, 4 or 5->0x8002EDF0, 6->0x8002F3C4, 8->0x8002F3E4, 9->0x8002E000, 10->0x8002E084, 11->0x800314B4, 12->0x800324D8, 13->(indirect). Only the 4/5 arm reaches the level load: 0x8002EDF0 -> 0x800144C8, which indexes a table at 0x8007A720 by [0x80075964]<<4 for the WAD offset and loads to [0x800785E4]. Runtime probes (game/core/level_load_probe.cpp, PSXPORT_DEBUG=lvl) show 0x8003385C entered repeatedly while 0x8002EDF0 is NEVER entered, and the mode reads 13 on the first entry and never changes for the whole run. Mode 13's arm reads [0x80078D78] and, when it is not 3, calls 0x8007ABAC — an address inside OVL0, and one of the four hardcoded out-of-text call targets that originally proved OVL0 was real code. So the port sits forever in an OVL0-handled stage.

## What would falsify it

A run where [0x800757D8] takes any other value, which would mean the mode is not stuck but merely slow to advance.
