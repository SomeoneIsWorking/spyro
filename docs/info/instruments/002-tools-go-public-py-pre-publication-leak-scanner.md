---
id: I002
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/go_public.py — pre-publication leak scanner

## Validated by

VALIDATED 2026-07-28 by the case that MUST differ: planted a file containing an absolute /home/<user> path -> scan reported 2 BLOCKING and named the file; removed it -> scan reported 0 blocking. So a clean result means it looked and found nothing, not that it silently stopped looking. NOTE: it was itself leaking when adopted (a baked USERNAME literal) — account names are now resolved at runtime from USER/LOGNAME/HOME plus GO_PUBLIC_NAMES.

## Known failure modes

(none recorded yet)
