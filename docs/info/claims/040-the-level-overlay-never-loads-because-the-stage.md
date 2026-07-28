---
id: C040
kind: claim
status: holds
created: 2026-07-28
tags: overlay,blocker,input
reconfirmed: 2026-07-28
---

## Claim

The level overlay never loads because the stage mode [0x800757D8] is stuck at 13 and the load only runs at mode 4 or 5

## Evidence

main calls the stage dispatcher 0x8003385C unconditionally at 0x80012230, and that function's whole body is a switch on the global [0x800757D8]. Decoded arms: 1->0x8002DF9C, 2->0x8002E12C, 3->0x8002EB2C, 4 or 5->0x8002EDF0, 6->0x8002F3C4, 8->0x8002F3E4, 9->0x8002E000, 10->0x8002E084, 11->0x800314B4, 12->0x800324D8, 13->(indirect). Only the 4/5 arm reaches the level load: 0x8002EDF0 -> 0x800144C8, which indexes a table at 0x8007A720 by [0x80075964]<<4 for the WAD offset and loads to [0x800785E4]. Runtime probes (game/core/level_load_probe.cpp, PSXPORT_DEBUG=lvl) show 0x8003385C entered repeatedly while 0x8002EDF0 is NEVER entered, and the mode reads 13 on the first entry and never changes for the whole run. Mode 13's arm reads [0x80078D78] and, when it is not 3, calls 0x8007ABAC — an address inside OVL0, and one of the four hardcoded out-of-text call targets that originally proved OVL0 was real code. So the port sits forever in an OVL0-handled stage.

## What would falsify it

A run where [0x800757D8] takes any other value, which would mean the mode is not stuck but merely slow to advance.

## Re-confirmed 2026-07-28

REFINED — one detail in the original wording was wrong. 'The port sits forever in an OVL0-handled stage' overstates it: the MODE [0x800757D8] is indeed stuck at 13 for the whole run, but the SUB-STATE [0x80078D78] does advance, 0 -> 3, and at 3 the mode-13 arm stops calling 0x8007ABAC (OVL0) and calls 0x80032B08 instead. The handler pointer [0x800758CC] then goes 0 -> 0x8008772C and the port dies calling it. So the game is progressing through the stage, selecting a level and installing that level's handler — it is not frozen waiting at a single point. Everything else in C040 stands: only the mode 4/5 arm reaches the level load, and it is never entered.
