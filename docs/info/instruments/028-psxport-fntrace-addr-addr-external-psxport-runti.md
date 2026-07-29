---
id: I028
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

PSXPORT_FNTRACE=<addr>[,<addr>...] (external/psxport/runtime/recomp/fntrace.cpp) — 'did control REACH this guest function, and from where?'. Fills the gap that watchpoints and hostprof both leave: a watchpoint only reveals blocks that STORE to a watched address, and hostprof resolves host PCs to whole functions, so every block of a 5000-instruction handler looks alike. It answers a BLOCK question by proxy — distinct paths make distinct calls, so trace a callee unique to the path in question. Implemented as an override that clears itself, re-dispatches to run the real body, then restores, so behaviour is unchanged. Reports 'NEVER CALLED' explicitly, and dumps on SIGTERM/SIGINT as well as atexit because these runs are killed by a timeout.

## Validated by

Validated on a KNOWN-POSITIVE and a KNOWN-NEGATIVE together, which is what caught it lying the first time. 0x8001F798 (which the host profile independently puts at 0.99% of samples) reports REACHED with 71558 calls, first at frame 436 from ra=0x8007DCAC; 0x8006276C reports NEVER CALLED, matching the independent ndiff finding that it is never invoked. THE FIRST VERSION FAILED THIS TEST: it installed before the game's override registrations and was silently displaced by them, reporting 'NEVER CALLED' for the CD loader, which runs 14 times a boot — the worst failure a tracer can have. Fixed by initialising LAST. The mirror hazard is documented in the file: it claims the override slot, so do not trace an address whose override does real work (the CD loader serves disc data); tracing a differentially-verified native body is harmless because re-dispatch runs the equivalent recompiled body. Also limited to MAIN entries (overlay modules expose no per-overlay override setter) and counts recursion once.

## Known failure modes

(none recorded yet)
