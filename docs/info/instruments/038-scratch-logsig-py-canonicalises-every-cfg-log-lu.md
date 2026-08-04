---
id: I038
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

tools/logsig.py — canonicalises every cfg_log*/lucent:: and CfgLine/lucent::Line call site in a C/C++ file to one message template, so a cfg_*->lucent sweep can be diffed template-for-template instead of trusting a run that only exercises a fraction of the sites.

## Validated by

Ran --selftest (19 sites, printf and std::format twins must canonicalise identically, plus a must-NOT-compare-equal case). Validated end to end by deleting ONE known template from a copy of the converted tree: the diff reported exactly that line and the count went 519->518. It REFUSES (exit 3) rather than printing an empty result when a scan finds zero sites. Caught a real conversion question in 5 separate places; in every case re-examination showed the CONVERSION was right and the TOOL's model was wrong (integer width/'+' flag, printf-escaped %% in an already-converted string, doubled braces, the space flag matching '% pixels'). Each was fixed with a selftest case. It still cannot see argument ORDER.

## Known failure modes

It cannot see argument ORDER — two call sites whose format string is identical but whose arguments are
swapped canonicalise to the same template and compare equal.

It LIVED IN scratch/ (gitignored) until 2026-08-05, which meant this registry entry pointed at a file
no other machine, clone or subagent could open, and that nothing would have reported as broken. Moved
to tools/. An instrument in a gitignored directory is not an instrument.
