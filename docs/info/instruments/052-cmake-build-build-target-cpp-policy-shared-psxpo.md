---
id: I052
kind: instrument
status: trusted
created: 2026-08-21
---

## Instrument

cmake --build build --target cpp-policy (shared psxport Clang policy checker)

## Validated by

On 2026-08-21, restored compare_global_words(Read read) in game/core/actor_chain_oracle.cpp to force a performance-unnecessary-value-param diagnostic. The target failed nonzero and named the exact file, line, and check while linting all 35 compile-backed first-party Clang TUs. Restoring const Read& made the same target pass: 46/46 first-party files formatted, 46/46 size-checked with three frozen legacy caps, and 35/35 TUs clang-tidy clean.

## Known failure modes

(none recorded yet)
