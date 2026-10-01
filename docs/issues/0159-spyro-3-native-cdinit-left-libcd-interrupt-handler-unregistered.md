---
id: 159
title: Spyro 3's native CdInit published callback words but never registered libcd's interrupt handler, so every CD interrupt ended in intr timeout
status: resolved
symptom: guest stdout "intr timeout(0004:000d)" from the libapi dispatcher 0x8005CA18 after the first CD interrupt
tags: spyro2,spyro3,cd,interrupt,cdinit
created: 2026-10-01
updated: 2026-10-01
---

## Answer

Root cause: the native `cdInitSuccess` wrote the four callback words CdInit's success path stores and set
`I_MASK`, but retail CdInit also runs `CD_init` (Spyro 3 `0x80061C8C`), which registers libcd's own
interrupt handler through `InterruptCallback(2, 0x800621CC)` (libapi `0x8005C6B4`). With no handler
registered the libapi dispatcher found the CD interrupt unowned and printed `intr timeout` (`0x8005CA18`).

Fix: neither title binds CdInit any more. The retail body runs through Lightrec and establishes the same
lifecycle and interrupt invariants itself, so there is one implementation (the guest's) for both titles and
no hand-copied constants to drift (`titles/spyro{2,3}/core/spyro*_runtime.cpp`; the dead constants and
`armCdInterrupt` calls are deleted). Spider-Man 1 keeps a copy of the native pattern
(`spider1_cd_initialization.cpp`); it is the same defect class and is a migration candidate, not edited here.

## Result

Spyro 3 captures at 300..2400 steps: no `intr timeout` in 4500 fields (it was present before). Spyro 2 shows
the same absence of `intr`/`CD timeout` lines. Both reach the title screen (issue 0160, 0161).
Unverified: Spyro 3 `CD_init` still prints one "CD timeout ... CdlReset" at boot; the controller emulation
does not answer the init reset command and libcd recovers after its retry.

