---
id: I025
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

tools/catalog.py stale — lists OPEN issues tagged 'blocker'; wired into gate.sh as a WARN reported only when the gate PASSES. A blocker asserts the port cannot get past it, so a passing gate makes each one a contradiction: either it no longer blocks (resolve it) or the gate does not cover it (say so in the entry). Deliberately dumb — no symptom parsing, no guessing which would not be trustworthy. WARN not FAIL, because the resolution is a human judgement and failing the build on it would teach everyone to ignore the line.

## Validated by

Made to show the OTHER answer rather than a constant: it reported 5, then 3 after issues #8 and #12 were resolved, and prints 'no open blockers' when the set is empty. Its first run found 5 genuine suspects, of which 2 were stale-open (work long since shipped) and 3 were real-but-uncovered (#20/#21 are about paths a 40s boot run need not exercise, #27 needs button input the headless gate never supplies) — so it correctly does NOT assert 'fixed', only 'contradiction, look here'. Each of the 3 now carries a note saying why the gate cannot settle it.

## Known failure modes

(none recorded yet)
