# SpyroEngine — Spyro 1, 2, and 3

A work-in-progress native PC port of the original PlayStation Spyro trilogy. The target architecture
combines title-owned native subsystems with psxport's runtime Lightrec executor for every remaining
MIPS instruction read from the player's authenticated game files.

No game code, asset, executable, disc image, or BIOS is distributed. Supply a legally obtained copy.

## Title status

| Title | Executable identity | Current evidence |
| --- | --- | --- |
| Spyro the Dragon | `SCUS_942.28` | The retired generated-code product reached stage 13, the native title/save flow, stage 14, and a controlled stage-0 FIELD route. Native rendering, widescreen, temporal presentation, input, CD, and audio owners exist, but Lightrec gameplay and complete visual/oracle coverage do not. |
| Spyro 2 | `SCUS_944.25` | Identity and crt0 facts are measured. The prior bring-up reached three host-owned black display fields and stopped at `0x80011B1C`; later boot and gameplay are absent. |
| Spyro 3 | `SCUS_944.67` | Executable identity and crt0 facts are measured. Disc provenance and product execution are absent. |

The executable serial, PS-X EXE header, size, and hash jointly select a title. A title never borrows
another title's addresses, runtime images, or capability policy.

## Migration status

The former product emitted guest code as C and compiled it into title-specific executables. That
architecture is retired. Do not regenerate, build, or run it for new evidence. The replacement plan
is [`docs/migration.md`](docs/migration.md).

Spyro 1's first implementation discriminator is the pair of already-recorded stage-13 routes:

- the 800-field boot/title route; and
- the 900-field forced-input mode-2 save-picker route.

Both must run through Lightrec with the existing native `Spyro1FrameDriver` and `FieldScheduler`,
nonzero translated blocks, one presentation fence per host step, and fatal guest VSync. The generated
world-body include must be replaced by resumable execution of the unchanged retail guest body through
explicit executor exits.

These 800/900 routes prove wiring only. Deleting the old pipeline requires a later representative
interactive gameplay milestone that proves native and scoped-original calls, positive and negative
invalidation across address-reusing WAD images, independent-oracle state, no linked/selectable
interpreter, and correctness/frame-time budgets on each released host architecture.

## Intended player experience

The finished `./run.sh` path will enter the frozen `uv` environment, authenticate the selected disc,
build the native/Lightrec product without offline guest translation, and launch it. The current
launcher still drives the retired generated pipeline and is therefore not a valid product or
verification route during migration.

Player builds will accept the supported GCC, Clang, and AppleClang toolchains. Maintainer C++
verification uses Clang with the tracked `clang-format` and `clang-tidy` policy. Missing native
dependencies must be refused with an exact user-run package command; Ghidra and other RE tools are
never player prerequisites.

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
