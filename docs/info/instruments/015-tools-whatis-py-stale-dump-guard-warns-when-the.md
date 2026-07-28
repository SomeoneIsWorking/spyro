---
id: I015
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/whatis.py stale-dump guard — warns when the RAM dump predates the port binary

## Validated by

Fires on the real case that motivated it: with no miss left to trigger a fresh dump, the file was 0.6h old and older than the rebuilt binary, and whatis now says so before printing residency. Verified it stays quiet when the dump is newer. This matters because the failure is silent otherwise — a stale dump keeps answering 'resident' with whatever was live at a fail-fast the current build no longer reaches, which is the same wrong-image error as C065, just aged rather than misfiled.

## Known failure modes

(none recorded yet)
