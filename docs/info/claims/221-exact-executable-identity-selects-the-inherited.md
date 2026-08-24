---
id: C221
kind: claim
status: holds
created: 2026-08-22
tags: runtime,identity,spyro2,spyro3
depends: game/core/title_selection.cpp#selectExecutable, game/core/main.cpp#main, game/core/title_runtime_registry.cpp#runtimeFor, tools/generate_title_catalog.py#render, tools/run.py#execute, titles/spyro3/core/spyro3_runtime.cpp#Spyro3Runtime
reconfirmed: 2026-08-24 19:44:11
verified_at: 2026-08-24 19:44:11
---

## Claim

Exact executable identity selects the inherited Spyro title runtime before Game construction

## Evidence

The shipping C++ selector uses the executable basename as an exact serial, then checks size, six PS-X EXE header facts, and SHA-256 before returning a title. The three JSON manifests are the single facts authority; the build generates the production C++ catalog from them. Its both-answer test selects a matching fixture, refuses an unsupported serial, refuses mutated bytes, and refuses valid bytes renamed to a different known serial. The launcher routes explicit `spyro1`/`spyro2`/`spyro3` codewords through only the matching manifest, cache, and executable argument. Real SCUS_942.28 selected Spyro1Runtime and completed a 120-field native run. Real 380928-byte SCUS_944.67 matched all 11 manifest facts, shipping crt0_extract resolved 8/8 boot fields, selected Spyro3Runtime, and emitted the explicit no-substrate refusal before Game. Spyro2Runtime and Spyro3Runtime bind no Spyro 1 legacy config/hooks. The authoritative Clang verifier passed 30/30 CTests including cpp-policy, generated-catalog coverage, per-title launcher routing, and the framework pin check.

## What would falsify it

Any supported-serial executable reaches Game with a different title runtime, any known serial accepts changed bytes/header/hash, any manifest change fails to reach the generated production catalog, any title codeword routes another title's cache/executable, any unknown serial falls back to Spyro 1, or a Spyro 2/3 runtime gains Spyro 1 compatibility views or executes without a verified substrate

## Re-confirmed 2026-08-24 19:44:11

Fresh authoritative Clang verifier on 2026-08-24 passed 30/30 CTests against recorded psxport d2266f4b, including exact/mutated/renamed selection, three-codeword launcher routing, manifest-generated catalog coverage, inherited Spyro 2/3 no-legacy/no-substrate runtimes, formatting, structure caps, clang-tidy, and pin check.
