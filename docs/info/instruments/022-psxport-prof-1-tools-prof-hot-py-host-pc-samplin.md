---
id: I022
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

PSXPORT_PROF=1 + tools/prof_hot.py — host-PC sampling profiler resolving to guest functions

## Validated by

Validated by CATCHING ITS OWN RESOLVER BUG on first use. It initially reported 'guest code: 0.0% of samples', which was an artifact of a regex anchored at end-of-string: nm reports MANGLED C++ names (_Z17gen_func_800258F0P4Core), so the guest address is not last. It also dumped 35% of samples onto '_end' by falling back to the nearest preceding symbol for addresses outside any sized symbol — really shared libs and the loader, now labelled as such. After both fixes it discriminates: two independent runs agree at 4.5% and 4.9% guest code, and it surfaced a hot guest function (0x800258F0) that the STATIC caller ranking rates at 2 callers and would never have suggested. The interpreter-based profiler in interp_diag.h cannot answer this at all — this port has no interpreter, so it would report nothing and the nothing would look like an answer.

## Known failure modes

(none recorded yet)
