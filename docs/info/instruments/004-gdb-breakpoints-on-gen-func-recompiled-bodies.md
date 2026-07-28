---
id: I004
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

gdb breakpoints on gen_func_* recompiled bodies

## Validated by

PARTIALLY TRUSTED. Breaking on framework C++ symbols (cd_sync) worked and gave a correct, decisive answer. But a breakpoint on gen_func_8002BBE0 did NOT hit within 120s even though a stack profile had shown that function executing — dispatch reaches bodies through the func_<addr> wrapper, and gdb+recomp startup is slow enough that a 120s window may simply not cover the phase. Do not read a non-hit as 'this code never runs'. Prefer in-process cfg_dbg instrumentation for guest-state questions.

## Known failure modes

(none recorded yet)
