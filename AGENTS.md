# SpyroEngine agent instructions

This repository targets the original PSX Spyro trilogy as native PC products whose remaining guest
instructions execute through psxport's runtime Lightrec integration. `CLAUDE.md` is a symlink to this
file. Read `../AGENTS.md`, `external/psxport/AGENTS.md`, and
`docs/migration.md` before changing execution architecture.

Goals are in `docs/project-goals.md`, status in `docs/project-state.md`, placement in `docs/codemap.md`.

## Product execution contract

- Each gameplay product is one native-plus-dynarec runtime. Native owners replace verified title
  behavior; Lightrec translates every remaining instruction on demand from the authenticated
  executable and currently resident WAD image.
- Interpreter-only execution is test-only and must never be selectable by a gameplay executable.
  The shared framework may admit its classified, bounded, accounted fallback only after a JIT
  refusal; an unfinished backend never authorizes interpreter-first execution.
- Offline guest translation is retired. Do not regenerate, build, run, extend, or diagnose the old
  generated-C product. Static analysis may produce reviewable symbols and non-executable metadata,
  never guest function bodies.
- The generator, emitted corpus, emission-only seeds, generated dispatch/tests, and old
  provisioning/build route are already removed break-first. Do not restore any of them as a product
  oracle, compatibility mode, or fallback while the executor is incomplete.
- Cache, overrides, and original calls use complete image identity. Spyro has many WAD images that
  reuse the same load address, so guest address alone is never sufficient. A scoped original call
  bypasses only its current override and executes through Lightrec.
- WAD loads, executable-memory writes, savestate restore, and override-table changes invalidate all
  affected translated blocks. Frame suspension, interrupts, exceptions, and termination use bounded
  executor exits rather than host-stack unwinding assumptions.

## Preserved binary and behavior facts

- Spyro 1 is `SCUS_942.28`, entered at `0x8005B8E0`. The disc boots that executable directly; there
  is no SCEA stub. Its runtime images live inside `WAD.WAD`, and many reuse one guest load address.
- Spyro 1 game main is `0x80012204`; boot ownership includes `0x800127C0` and `0x8001286C`.
  Historical evidence assigns the 60 Hz counter, input, callback root, audio, BIOS events,
  presentation, pacing, and host-turn acknowledgement to a title-local field owner.
- The recorded dynamic discriminator must preserve stage 13 mode 0/1 title presentation and mode 2's
  three-slot save picker at the existing 800- and 900-field caps.
- Spyro 2 is `SCUS_944.25`, entry `0x8005478C`, game main `0x80011ADC`, display bootstrap
  `0x80011BBC`, and libetc VSync `0x80058EDC`. Its current measured boundary stops after three black
  display fields at `0x80011B1C`.
- Spyro 3 is `SCUS_944.67`, entry `0x80059444`, game main `0x8001200C`, and libetc VSync
  `0x8005956C`. Disc provenance and product execution remain unverified.

## Title selector

The zero-argument `spyro_port` opens an in-window selector over Spyro 1/2/3 (`game/host/`); `./run.sh` provisions every
title whose disc is configured and launches it. `spyro_port <executable>` is a maintainer override that skips the
selector and runs that one serial-identified executable (drivers such as `tools/drive.py` use it). Headless:
`pick <slug>` / `session return` on the control channel; proves a switch is the same run as a
fresh process. Issue 0169.

## Working discipline

- Look up addresses with Ghidra (`external/psxport/tools/decomp_pipeline.py --image scratch/assets/<title>/<exe>
  --target/--callers/--refs/--function-at 0xADDR`) instead of guessing or reading one address at a time.
- Native owners stay cohesive: frame/field lifecycle, rendering, input, audio, storage, CD/archive and title
  selection are separate owners, not piled into `main.cpp` or `render_frame.cpp`.
- **Agents never run `./run.sh`.** Run the product with `tools/drive.py`, `tools/title_route.py`, `tools/shot.py`,
  or the binary through `external/psxport/tools/port/launch_environment.py::agent_environment` (offscreen, silent).
  Headless button presses go through a forced-input path, not the player's SDL path: when a fix is about input,
  check it through the live path too.
- Check work by running the game and looking at the capture. Gate once at the end:
  `uv run --frozen python tools/verify.py --jobs 6`.

No game asset, executable, disc image, generated guest body, or machine-specific path is committed.
