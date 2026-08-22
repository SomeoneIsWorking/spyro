---
id: I054
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

PSXPORT_FIELD_ENVIRONMENT_ORACLE — joins retail 0x8002B9CC to its one 0x800258F0 call and compares the shipping semantic boundary recipe

## Validated by

Positive real run scratch/logs/gate-boot-20260822-181019.log reported 523/523 matches and 1,555 foreign calls outside the join; the complete gate reported 13 PASS and zero failures against the recorded framework pin ad5cf802. tests/test_field_environment_recipe.cpp invokes the exact field_environment::matches implementation used by worldEntry and proves it returns the other answer for a changed selection, changed distance, and one uncleared work byte; the matching positive remains accepted.

## Known failure modes

It only owns override slots on `PSXPORT_RENDER_PATH=gte`; native subprocesses explicitly refuse it.
World-owner diagnostics targeting `0x800258F0` are mutually exclusive and abort during registration
instead of silently displacing one another. A run that never reaches FIELD aborts at finish rather
than reporting a zero-denominator pass.
