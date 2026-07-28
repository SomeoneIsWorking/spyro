---
id: I021
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

PSXPORT_NDIFF COP2/GTE comparison — the differential now covers the GTE register file, not just GPRs and RAM

## Validated by

Validated in both directions after the extension. (1) Perturbing one COP2 data register inside an otherwise-correct native body is caught and NAMED: 'cop2 DR12: native=0x009AFDA7 substrate=0x009AFDA6' on call #1. (2) With the perturbation removed, all ten existing native bodies still report 200/200 matches and zero divergences, so the added comparison does not produce false positives. This closed a real blind spot rather than a theoretical one: 0x800171FC (87 callers) is GTE code whose entire effect can be in REG[0..63]/FLAGS, none of which is guest RAM — the differential would have reported 'matches' on a native body that silently left the GTE state different, which is worse than no check.

## Known failure modes

(none recorded yet)
