---
id: C169
kind: claim
status: disputed
created: 2026-08-12
tags: producers
depends: game/core/producer_run.cpp, game/core/vsync.cpp#deliver_field
---

## Claim

The producer DB now EMITS in this port, and what it emits is the designed negative: a capped headless run (PSXPORT_NATIVE_FRAMES=2000) prints the [producers] run-end report and writes scratch/producers/run-*.jsonl with 0 rows, 0 claims, 1718994 prims seen = 859497 guest-origin + 859497 span-miss, 0 attributed, 0 unscoped-native. That reads NOT MEASURED (the packet pool is blind, packetPoolBase/Stride are 0 in game/core/game_config.cpp, and no ProducerScope exists in the tree), never 'this game draws nothing'.

## Evidence

scratch/logs/producers_run.log lines 36-39 (rc=0, cap 2000) and scratch/logs/producers_run2.log (rc=0, cap 600, 197746 prims = 98873 + 98873). Lifecycle: game/core/producer_run.cpp, called from main.cpp (begin, before dc_boot_init) and vsync.cpp's vblank field (per presented frame). An UNCAPPED run still emits nothing and now SAYS SO at boot ([producers:warn] in scratch/gate/run.log:6), because it is killed by signal — tools/gate.sh requires rc=137.

## What would falsify it

a capped run that prints no [producers] run-end line or writes no scratch/producers/run-*.jsonl; or a run whose report shows nonzero attributed rows while packetPoolBase is still 0 and no ProducerScope exists (that would mean the numbers come from somewhere unaccounted)

## OPERATOR CHECK 2026-08-12: the LIFECYCLE half reproduces, the FED half DOES NOT

Split the claim in two, because only one half survived my re-run.

**REPRODUCES — the lifecycle.** A capped run prints the `[producers]` boot-armed line, the frame-cap end
line, the claim-resolution line and the exit `[cfg]` audit, exits rc=0, and `tools/gate.sh 40` passes
(rc=0, 0 FAIL). That is the part this stream was for, and it holds.

**DOES NOT REPRODUCE — the fed numbers.** This claim cites 197,746 prims at cap 600
(`scratch/logs/producers_run2.log`). FIVE operator runs report the opposite:
`[producers:warn] run-end: the producer census was NEVER FED this run — 0 notes reached it`, and the
framework correctly REFUSES to write a JSONL for them. Runs: cap 600 and cap 1800 against the framework
DEV CLONE; cap 600 with the claim's exact executable path (`scratch/bin/spyro/SCUS_942.28`, ruling out
asset lookup relative to the exe); and cap 600 built against the PINNED submodule in a separate
`build-pin/` (ruling out my two later framework commits as a regression — that A/B was the reason to
suspect them, and it cleared them). Logs: `scratch/logs/audit-check.log`, `audit-1800.log`,
`pathtest.log`, `leg-pin.log`.

**So one of two things is true and I could not tell which.** Either the feed is NON-DETERMINISTIC at a
fixed frame cap — plausible, since the cap counts PRESENTED frames and this port was under heavy parallel
load when the claim was measured, so 600 presents need not reach the same game state — or the claimed runs
had environment this repo no longer has (a memory card with save data, a different disc-data layout). The
claim's own evidence logs are still on disk, so this is decidable: re-run at several caps, and if fed and
unfed both occur from the same command, the cap is not a reproducible unit and the DB's denominator for
this port cannot be quoted per-run.

**Consequence, and it is the reason this is downgraded rather than annotated:** `0 rows` here must not be
read as "measured, nothing to attribute". It is either "not measured" (pool blind, no ProducerScope — the
claim is right about that) or "the feed did not execute at all", and the framework prints a DIFFERENT
sentence for each. Quote the sentence, never the row count.

## 2026-08-12, from #59's verification: what the FED/UNFED split actually depends on, and the 2x in every number here

Two things this stream could not separate are now measured, and neither rescues the disputed figures as
stated.

**FED vs UNFED at a fixed cap is a BOOT-PROGRESS question, and the cap is the wrong unit for it.**
`PSXPORT_NATIVE_FRAMES` counts PRESENTED FIELDS, and nothing is pushed to any census until the run reaches
drawn frames — with the frame loop armed, `spyro_frame_loop_run` first runs `rc0(0x800127C0)` (CD loads +
logo fades) and the cap can fire inside that call, before the loop's first iteration. Measured this
session, identical command, only the cap changed: **cap 400 -> `NEVER FED`, 0 notes, JSONL refused
(`scratch/logs/prod59_run1.log`); cap 3000 -> fed, a row written (`prod59_run3.log`)**. So a cap in the
hundreds sits ON the boundary, which is exactly where the claim's cap-600 figure and the operator's five
cap-600 `NEVER FED` runs both live. The honest reading is the operator's: **quote the sentence the report
prints, and do not treat a frame cap as a reproducible denominator for this port** until the cap counts
something that starts after boot.

**The prim figures in this claim are 2x the real prim count — see issue #61.** `197746 = 98873 + 98873`
and `1718994 = 859497 + 859497` are not partitions: `guest-origin` and `span-miss` each increment
`prims seen` for the SAME prim (`producer_census.h` lines 170 and 173), so a reference-leg run counts
every prim twice. Reproduced here as `2962984 = 1481492 + 1481492`. The real figure in each pair is the
summand, never the total. That is a framework accounting defect, not evidence about this port.

**The part of this claim that is now superseded rather than disputed:** "no ProducerScope exists in the
tree" was true when written and is no longer — issue #59 landed one (C170), so a native-leg run here
reports `1 row(s) ... attributed 1380 ... unscoped-native 0`. `0 rows` on a native leg is now a real
negative to investigate, not the expected state.
