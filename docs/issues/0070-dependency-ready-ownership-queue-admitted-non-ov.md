---
id: 70
title: Dependency-ready ownership queue admitted non-overridable boundary artifacts
status: resolved
symptom: own_candidates --ready-nonleaf listed jr-ra split addresses that FNTRACE and shard_set_override refused; every real <=60-instruction candidate was cold in the 3,000-field shipping path.
tags: ownership,tooling,reach
created: 2026-08-21
updated: 2026-08-21
---

## Root cause

`own_candidates.py` finds approximate function boundaries by splitting at `jr ra`. A mid-function
return therefore produced a plausible small body with owned direct callees even though the generated
MAIN dispatcher had no entry for that address. Dependency readiness alone did not imply that the
runtime could install an override. Separately, its ownership denominator counted only bodies inside
the active `--maxsize` queue, so the large generated world owner disappeared from the reported total.

## What was tried / dead ends

The first expanded trace requested 13 addresses. FNTRACE armed ten and explicitly refused
`0x8003DF60`, `0x8003A79C`, and `0x80038AFC` as non-entries. This was not FNTRACE's 16-site limit:
the other three were boundary artifacts, and retrying them would never make them ownable.

## Resolution

### Resolution (2026-08-21)

The authoritative ranker now intersects its approximate boundaries with the generated declaration
inventory before calling a non-leaf dependency-ready, and reports the complete source-derived owned
set independently of the queue's size window. `--addr 0x8003DF60` explains that the body is not
overridable instead of silently recommending it. With artifacts removed, all ten real candidates of
at most 60 instructions were cold in a 3,000-field run; expanding the same filter to 120 instructions
found the reached PsyQ WriteSpuRamPio `0x8005BE88`, whose real frame-zero call matched the retained
generated body under NDIFF.
