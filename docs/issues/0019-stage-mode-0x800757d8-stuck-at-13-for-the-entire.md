---
id: 19
title: Stage mode [0x800757D8] stuck at 13 for the entire run — nothing advances it
status: open
symptom: The game never leaves the OVL0-handled stage 13, so the level-load arm (modes 4/5) never runs and no level overlay is ever loaded. Downstream this is what makes the handler call at 0x80014478 land in never-written memory (issue 0017).
tags: blocker,stage,input
created: 2026-07-28
updated: 2026-07-28
---

See C040 for the dispatch table and the probe evidence.

WHAT IS ESTABLISHED
  * main -> 0x8003385C is unconditional; that function switches on [0x800757D8].
  * Only the mode 4/5 arm reaches the level load (0x8002EDF0 -> 0x800144C8).
  * Probes show 0x8002EDF0 is NEVER entered and the mode reads 13 from the first
    dispatcher entry and never changes.
  * Mode 13 calls 0x8007ABAC inside OVL0 (unless [0x80078D78]==3, which routes to 0x80032B08).

WHAT IS NOT YET ESTABLISHED — do not assume it
The obvious story is 'the title screen is waiting for a button press and input never arrives' (C035:
the game never reads the pad itself, so the BIOS/HLE must supply it, and GameConfig's pad group is all
zero). That is PLAUSIBLE AND UNTESTED. Mode 13 may equally be an intro/attract stage that should advance
on a timer, or on a CD/stream completion, or on state that a native override is failing to set.

WRITER MAP (step 1 done). 34 writers, ALL in resident text, ZERO in OVL0. The constant each stores,
where statically determinable:
   0 -> 0x80013B4C 0x80016268 0x8002C588 0x8002C808 0x8002C8C8 0x8002CB8C 0x8002D040 0x8002E070
        0x80062854 0x80067440      1 -> 0x8002C6C8 0x8002D7BC 0x80057208      2 -> 0x8002C468
   3 -> 0x8002C77C      5 -> 0x8002C888      7 -> 0x8002EAE0      8 -> 0x8002CA3C
   9 -> 0x800162C8     10 -> 0x8002C630     11 -> 0x8002CCF4     13 -> 0x8002D18C 0x8002D4C4
  14 -> 0x8002D35C 0x800336C4    15 -> 0x8002D2A4    -1 -> 0x80062BB0
  computed -> 0x80024E38 0x80025734 0x80026F34 0x80027014 0x80027E84 0x80029288 0x800593DC

Two things worth noting. The 13-writers 0x8002D18C and 0x8002D4C4 sit in the same function region as the
loader call site 0x8002D31C, so mode 13 looks like the INTENDED state right after the boot overlay
loads — the port is not in a corrupt state, it is in the correct one and simply never leaves. And a
writer for the level-load mode does exist: 0x8002C888 stores 5.

NEXT, in order:
  2. DONE. [0x80078D78] is NOT stuck: probes show it advancing 0 -> 3, after which the mode-13 arm
     calls 0x80032B08 instead of OVL0's 0x8007ABAC, and the handler pointer [0x800758CC] is then
     installed as 0x8008772C and called — which is the crash. So the game is progressing through the
     stage and SELECTING A LEVEL; it is not frozen at one point. C040 refined accordingly.
  3. DONE (C041, tools/callgraph.py). 0x80032B08 has no direct path to the CD loader or to the level
     load; OVL0 calls neither; the only callers of the level load 0x800144C8 are the mode 4/5 arm and
     the dispatcher above it. The mode-5 writer 0x8002C888 lives in fn 0x8002C85C, whose callers are
     0x80042F10 and 0x8004A4D8. So the transition into the level-loading mode EXISTS in resident code
     and is simply never taken.
  4. DONE. 0x80042F10 is in fn 0x80041670 and 0x8004A4D8 in fn 0x8004A200; both are reached only from
     the mode 8 and mode 9 arms (and the dispatcher directly at 0x80033AD8). The mode-13 handler has no
     direct path to either. So reaching mode 5 requires first reaching mode 8 or 9.

  5. DONE, and it reframes the problem (C042). The intro stage is NOT idling — it is actively trying to
     bring up a level. 0x80032B08 is the level-load SETUP: it reads the arena base [0x800113A0] and
     writes the arena cursors, its sub-sub-state [0x80078D7C] advances 0->1->2, and the arena cursor
     advances 0x8007DDE8 -> 0x8008A3B8, allocating space well past the handler address. Then the
     handler is installed and called. But NO CD READ IS EVER ISSUED: the queue logs gate=0 pending=0
     queued=0 on every tick. Not stalled — never asked.

  6. NEXT: the guard [0x80078D94] reads 2 and skips the cursor reset at 0x80032B60. Find who writes
     that global and what value the real console would have there at this point. That is the first
     place the flow visibly diverges from what the code expects.
