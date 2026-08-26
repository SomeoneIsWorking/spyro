---
id: 84
title: re-frontier next still selected completed Spyro CD loader work
status: resolved
symptom: re_frontier.py next reported cd.pc-owned-stock-libcd even though both game-level loaders were already owned and verified, obscuring own.non-leaf as the actual ready frontier
tags: re-frontier,cd,ownership,stale
created: 2026-08-26
updated: 2026-08-26
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-26)
The entry remained todo with an obsolete whole-run-harness blocker after C106 had verified both user-selected game-level loader owners and I019 had established per-call NDIFF. It is now re-verified with C074/C106, the obsolete hardware-handler direction and blocker note are removed, and next reports only the in-progress non-leaf frontier.
