# SpyroEngine agent instructions

This repository targets the original PSX Spyro trilogy as native PC products whose remaining guest
instructions execute through psxport's runtime Lightrec integration. `CLAUDE.md` is a symlink to this
file. Read `../AGENTS.md`, `external/psxport/AGENTS.md`, and
`docs/migration.md` before changing execution architecture.

Project intent, factual coverage, placement, migration order, and binary-evidence order live in
`docs/project-goals.md`, `docs/project-state.md`, `docs/codemap.md`, `docs/migration.md`, and
`docs/re-frontier.md`. Begin non-trivial work with `python3 tools/info.py brief <terms>`, then consult
`python3 tools/re_frontier.py next` and the relevant issue.

## Product execution contract

- Each gameplay product is one native-plus-dynarec runtime. Native owners replace verified title
  behavior; Lightrec translates every remaining instruction on demand from the authenticated
  executable and currently resident WAD image.
- The interpreter is test-only. It may exist in a separately built diagnostic target but must not be
  linked into, selectable by, or reachable as a fallback from any gameplay executable.
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

## Spyro 1 first discriminator

Reach both recorded stage-13 routes through Lightrec: the 800-field boot/title route and the 900-field
forced-input mode-2 save-picker route. A new title-owned frame/field boundary must execute nonzero
dynamic blocks, satisfy one presentation fence per host step, and leave guest libetc VSync
`0x8005DBC4` fatal. The retired frame driver and scheduler are not compatibility paths.

`game/core/world_body.inc` and every generated-world-body call have been replaced by the
`WorldGuestExecution` scoped-original boundary. The unchanged world body must suspend and resume
through explicit executor exits while
native scene owners remain active. Do not transcribe, regenerate, or retain a generated body as a
shipping super-call.

The 800/900 stage-13 routes are first wiring discriminators, not representative gameplay. An
interactive gameplay route must prove native and scoped-original dispatch,
address-reusing WAD invalidation, independent-oracle state, no interpreter in the product, and the
declared correctness/performance budget on each released host.

Finish Spyro 1 before continuing title-specific Spyro 2 or Spyro 3 implementation. Preserve their
measured identities and boot facts, but do not extend their retired generated bring-up paths.

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

## Working discipline

- Never guess an address, image identity, or load base. Query the project registry and committed
  analysis metadata before reasoning from a guest address, and prefer runtime write/reach evidence
  over raw greps.
- Native render producers consume pre-GTE game state. Diagnostic replay may observe a guest body from
  a complete snapshot, but shipping presentation must not execute partial guest rendering or consume
  guest scratch/GTE output as native source state.
- Native owners remain cohesive: frame/field lifecycle, rendering, input, audio, storage, CD/archive,
  diagnostics, and title selection do not collapse into `main.cpp` or `render_frame.cpp`.
- Diagnostics report denominators, missing corpus, and both answers. Boot, logos, menus, FMV, and a
  clean trace do not establish gameplay conformance.
- Do not use `./run.sh` for agent verification; use focused build/test commands. The launcher is a
  player surface and now builds only the native/Lightrec product.
- Preserve verified binary addresses, behavior, native subsystem contracts, and real scenarios while
  replacing stale methodology. Update the one nearest living authority whose answer changes.

No game asset, executable, disc image, generated guest body, or machine-specific path is committed.
