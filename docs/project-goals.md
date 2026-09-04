# Project goals — SpyroEngine

These goals record durable product intent. Factual coverage is independent in
`docs/project-state.md`; atomic work lives in `docs/issues/`.

## G001 — Three authenticated Spyro PC products

Deliver Spyro the Dragon, Spyro 2, and Spyro 3 as distinct native PC products in one lineage
repository.

Why it matters: the titles share architecture, but each executable and runtime image has independent
identity, addresses, behavior, and readiness.

Success conditions:

- A supplied disc selects exactly one title by authenticated serial, PS-X EXE header, size, and hash.
- Each title owns its runtime image, native policy, and image-aware overrides without borrowing
  another title's facts.
- All three titles reach representative interactive gameplay through their own measured boundaries.

Constraints and non-goals:

- Human-readable names are labels, never selectors.
- Shared lineage behavior is extracted only after measured equivalence.
- The repository distributes no copyrighted executable, disc, BIOS, or game asset.

Related state: S001, S006, S008, S009, S010.

## G002 — Native/Lightrec gameplay architecture

Ship one execution architecture in which native owners replace selected behavior and Lightrec
dynamically executes every remaining guest instruction from the player's authenticated image.

Why it matters: offline-generated guest corpora create title-specific build products and source-level
control-flow workarounds instead of a reusable runtime execution boundary.

Success conditions:

- psxport owns a per-`Core` Lightrec executor, CPU/device synchronization, bounded exits, image-aware
  native overrides, scoped original calls, and executable-memory invalidation.
- Gameplay executables neither link nor select an interpreter and have no interpreter fallback.
- Fresh product builds emit no guest C/C++, object corpus, or precompiled title substrate.
- The generator, generated corpora, emission-only seeds, generated dispatch/tests, and obsolete
  provisioning paths are absent after representative-gameplay conformance.

Constraints and non-goals:

- An interpreter may exist only in a separately built test/diagnostic target.
- Runtime JIT code generation and an optional disposable cache are allowed; a pre-populated cache may
  not be a fresh-install prerequisite.
- Static analysis may produce symbols or non-executable metadata, never guest bodies.

Related state: S008, S009, S010.

## G003 — Native widescreen and temporal presentation

Render each title's owned scenes from pre-GTE game state with a wider projection and true temporal
presentation.

Why it matters: post-GTE packets have lost world depth and pre-quantisation motion, so they cannot
produce correct expanded view or stable per-object interpolation.

Success conditions:

- Complete scene producers use title state, source assets, and explicit painter/depth ownership.
- Widescreen expands the projection/viewport/scissor and any proven culling owner without stretching
  the final image.
- Compatible objects interpolate stable previous/current authored state without replaying guest
  rendering or interpolating guest packets.
- Unowned scene arms refuse explicitly rather than fabricating a plausible picture.

Constraints and non-goals:

- Guest scratch, GTE output, OT packets, and rendered VRAM are diagnostic evidence, never native-
  producer source state.
- Frame duplication or retiming guest output is not temporal interpolation.

Related state: S002, S003, S005, S007, S011.

## G004 — Portable, evidence-backed delivery

Make a fresh clone provision, build, and launch the selected native/Lightrec product through the
default launcher, with honest verification of gameplay and native features.

Why it matters: boot, a menu, a static trace, or a warm maintainer checkout does not establish a
shipping game.

Success conditions:

- `./run.sh` enters one frozen Python environment and launches the selected current product without
  offline guest translation.
- Missing native dependencies produce exact user-run platform package commands.
- Player builds accept supported GCC, Clang, and AppleClang toolchains; maintainer evidence uses
  Clang, `clang-format`, and `clang-tidy`.
- Each released host architecture passes a bounded representative interactive gameplay scenario with
  declared correctness and frame-time budgets.
- Hermetic, runtime, registry, and negative-case checks print their exercised denominators and refuse
  missing corpus.

Constraints and non-goals:

- Ghidra and other maintainer RE tools are not player prerequisites.
- A clean boot or one image does not imply gameplay, audio, timing, or visual conformance.

Related state: S008, S009, S010, S012.
