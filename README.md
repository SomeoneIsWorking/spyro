# SpyroEngine — Spyro 1, 2, and 3

A work-in-progress native PC port of the original PlayStation Spyro trilogy. The target architecture
combines title-owned native subsystems with psxport's runtime Lightrec executor for every remaining
MIPS instruction read from the player's authenticated game files.

No game code, asset, executable, disc image, or BIOS is distributed. Supply a legally obtained copy.

## Title status

| Title | Executable identity | Current evidence |
| --- | --- | --- |
| Spyro the Dragon | `SCUS_942.28` | Historical evidence reached stage 13, the title/save flow, stage 14, and a controlled stage-0 FIELD route. Verified native leaf/gameplay owners are image-scoped; the old frame/render/service integrations are removed. Lightrec gameplay and complete visual/oracle coverage do not exist yet. |
| Spyro 2 | `SCUS_944.25` | Identity and crt0 facts are measured. The prior bring-up reached three host-owned black display fields and stopped at `0x80011B1C`; later boot and gameplay are absent. |
| Spyro 3 | `SCUS_944.67` | Executable identity and crt0 facts are measured. Disc provenance and product execution are absent. |

The executable serial, PS-X EXE header, size, and hash jointly select a title. A title never borrows
another title's addresses, runtime images, or capability policy.

## Migration status

The former product emitted guest code as C and compiled it into title-specific executables. That
architecture is removed. The generator, corpora, seeds, generated dispatcher/tests, old build path,
and emitted world body are absent. The sole product accepts the authenticated executable and enters
psxport's runtime guest-execution boundary. The remaining work is in
[`docs/migration.md`](docs/migration.md).

Spyro 1's first implementation discriminator is the pair of already-recorded stage-13 routes:

- the 800-field boot/title route; and
- the 900-field forced-input mode-2 save-picker route.

Both must run through the linked Lightrec executor with a new title-owned frame/field boundary,
nonzero translated blocks, one presentation fence per host step, and fatal guest VSync. The emitted
world-body include has been replaced by an explicit scoped-original boundary for the unchanged
retail guest body; title-specific runtime-exit and route conformance remain unverified.

These 800/900 routes prove wiring only. A later representative interactive gameplay milestone must
prove native and scoped-original calls, positive and negative
invalidation across address-reusing WAD images, independent-oracle state, bounded fallback admission,
no interpreter-only product selector, and correctness/frame-time budgets on each released host
architecture.

## Intended player experience

`./run.sh` enters the frozen `uv` environment, authenticates the selected disc, provisions only the
PS-X EXE, builds the native/Lightrec product without offline guest translation, and launches it.
The frozen PSXport/Lightrec backend is linked and its synthetic framework contract passes. The
real-media execution boundary and remaining boot failure are recorded in
[`docs/project-state.md`](docs/project-state.md#s008--runtime-lightrec-execution). Lightrec remains the
sole product executor, with only the shared framework's bounded fallback after a classified JIT refusal.

Player builds will accept the supported GCC, Clang, and AppleClang toolchains. Maintainer C++
verification uses Clang with the tracked `clang-format` and `clang-tidy` policy. Missing native
dependencies must be refused with an exact user-run package command; Ghidra and other RE tools are
never player prerequisites.

Hosted CI is deliberately asset-free. Its Linux x86_64 source-policy job runs the canonical verifier
without downloading game files or claiming runtime gameplay. macOS arm64, Windows x86_64, and
Android arm64 runtime jobs remain partial/missing until platform packaging owners land. Maintainers
run `uv run --frozen python tools/verify.py` for the full local Clang build, formatting/clang-tidy/
structure checks on active first-party translation units and their paired headers, CTest, and
frozen-pin gate; title gameplay and real-media evidence remain separate requirements.

## Architecture

```text
authenticated executable / resident WAD image
                    |
                    v
        per-Core psxport Lightrec executor
          | dynamic cache | invalidation |
          +---------------+--------------+
                          |
            image-aware title dispatch
               |                    |
         native override      original guest body
                               through Lightrec
```

`game/core/` owns Spyro-lineage and Spyro 1 native services. `game/render/` owns cohesive semantic
producers and render orchestration. `titles/<title>/` owns executable identity and title-local
lifecycle/policy. `external/psxport/` owns the title-neutral executor and PSX services. Static analysis
metadata may remain; emitted executable guest bodies are not part of the product architecture.

See [`docs/project-goals.md`](docs/project-goals.md) for durable outcomes,
[`docs/project-state.md`](docs/project-state.md) for factual coverage,
[`docs/codemap.md`](docs/codemap.md) for ownership, and
[`docs/re-frontier.md`](docs/re-frontier.md) for the ordered binary-evidence chain.

## Legal

psxport's vendored beetle-psx backend is GPL-2.0. Spyro is a trademark of its respective rights
holders. This project is not affiliated with or endorsed by them.
