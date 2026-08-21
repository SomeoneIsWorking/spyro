---
id: C212
kind: claim
status: holds
created: 2026-08-21
tags: tooling,recomp,seeds
depends: tools/computed_jumps.py#seeding_guidance, tools/computed_jumps.py#guidance_errors, external/psxport/tools/recomp/test_emit.py#test_main_reentry_emits_a_wrapper_body_and_dispatch_case
---

## Claim

Spyro computed-jump guidance has one seed authority: heuristic case addresses are recognizer-owned, while a measured true re-entry uses main_reentry alone because that key is itself a resident discovery root

## Evidence

Current psxport emit.py unions main_reentry into the resident seed set and its test_main_reentry_emits_a_wrapper_body_and_dispatch_case positive supplies only main_reentry yet requires a wrapper, generated body, and dispatch case. The shipping computed_jumps.py output now refuses raw case seeding and names main_reentry only. Its 4/4 selftest accepts current guidance and rejects the historical BOTH main and main_reentry sentence, naming both missing discovery-root semantics and duplicate authority; the registered computed_jumps_selftest CTest passed after an exact 9f1bb927 Clang configure, and focused Ruff check/format passed.

The complete exact-tree CTest set then passed 12/12, including the new selftest and the existing
Clang format/tidy/structure gate over 41/41 compile-backed first-party C++ translation units.

## What would falsify it

if computed_jumps.py again recommends seeding heuristic cases or duplicates a true re-entry in main, its negative control accepts the historical BOTH sentence, or psxport no longer treats main_reentry alone as a discovery root plus fallthrough boundary
