---
id: 57
title: tools/gate.sh can FAIL on its first run after a full rebuild, then PASS unchanged
status: open
symptom: gate.sh prints [gate] FAIL with every visible check PASS; re-running the same binary gives rc=0 and zero FAIL lines
tags: gate,flaky,verification
created: 2026-08-12
updated: 2026-08-12
---

2026-08-12, seen while bumping the psxport pin to 63c5f537 (substrate fully regenerated, spyro_port relinked). The FIRST gate.sh run after that rebuild exited FAIL while every check it printed read PASS; the failing line was above the captured tail so it was not identified. THREE subsequent runs of the SAME binary: rc=0, zero FAIL lines. Most likely cause, NOT proven: gate.sh takes a fixed seconds budget (default 40) and the first run after a rebuild pays cold page-cache on a 40 MB binary plus freshly written generated/ shards, so a boot-progress threshold is missed. If this recurs, capture the FULL stdout (not the tail) to name the check, and consider whether the budget should scale or the gate should warm the binary first. Recorded because a FAIL that disappears on re-run is exactly what gets waved off as flaky and then hides a real regression.
