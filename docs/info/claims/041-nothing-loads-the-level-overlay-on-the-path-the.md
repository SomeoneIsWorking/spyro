---
id: C041
kind: claim
status: holds
created: 2026-07-28
tags: overlay,blocker
---

## Claim

Nothing loads the level overlay on the path the game actually takes: the only route to the load is the mode 4/5 arm, and mode 5 is set from two sites that never run

## Evidence

tools/callgraph.py (direct-call reachability, validated on two known-true paths before use: dispatcher->loader via the 4/5 arm, and 0x800144C8->0x80016500). Results: 0x80032B08, the handler the game reaches at mode 13 sub 3, has NO direct path to either the CD loader 0x80016500 or the level load 0x800144C8 across 306 explored functions. OVL0 makes no direct call to either. The ONLY callers of 0x800144C8 are 0x8002EDF0 (the mode 4/5 arm) and above it the dispatcher 0x8003385C. The mode-5 writer 0x8002C888 sits in fn 0x8002C85C, whose two callers are 0x80042F10 and 0x8004A4D8. So the transition into the level-loading mode exists in resident code and is simply never taken. CAVEAT the tool states itself: it cannot see jalr edges, and Spyro uses them heavily — so a negative is 'no DIRECT path', not proof none exists.

## What would falsify it

Finding an indirect (jalr) route from the mode-13 path to the loader, which this tool is blind to by construction.
