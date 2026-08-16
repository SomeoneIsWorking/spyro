# External references

Public reverse-engineering work on Spyro the Dragon. **Nothing is copied into this repo's own
sources** — these are pointers for cross-checking our RE (symbol names, function boundaries,
structure). Where a reference and our own measurement disagree, the measurement wins: this repo's
rule is that a claim cites evidence from the binary or a running port.

`open-spyro` is now a **submodule at `external/open-spyro`** (CC0), pinned to a commit so a checkout
is reproducible and the reference reaches subagents. It is a REFERENCE ONLY: never build it, never
copy its C into `game/`, and never let a symbol name stand in for evidence. What it is good for is
the two things that are expensive to derive from scratch and cheap to look up —

  * **Function boundaries and sizes.** `config/spyro.main.ld` carries `name = 0xADDR; // type:func
    size:0xN`. This settled C137 independently (0x80022A2C is 0x1098 = 1062 instructions, not the 598
    a lui-scan classifier had recorded) and confirmed four other sizes to within 20 instructions.
  * **Names, as a HYPOTHESIS to test.** They corrected the reading of C136 immediately — 0x8004F000 is
    `EmitStaticActorMeshListFogged`, so "never called in this capture" is a fogged-level variant, not
    a mystery. They also contradict part of the mute map (C138), which is what a reference is for:
    the disagreement is the finding, and the measurement decides it.

Its own progress notes are worth reading once: it excludes **92 hand-written assembly functions**
(~95 KB) as unmatchable from C because "they use `$at` as a data register" — that is exactly this
port's geometry-renderer family, and independent confirmation of the `$at`/`$ra`-as-data idiom that
caused the upstream `jr $ra` mis-classification in issue 0040.

`spyro-1` is a **submodule at `external/spyro-1`** (CC0), pinned to a commit like `open-spyro`. It is the
**primary decomp reference** for this repo — the most complete matching decompilation of SCUS_942.28 known
to this workspace (its README claims >40%; its own `progress.md` tracks 242 of 270 listed functions as
matched, ~2.7x `open-spyro`'s figure). Verified 2026-08-16: its target `PSX.EXE` is SHA-1
`84e3728ab94720d0873e2514adf4aade4935e0c5`, **byte-identical to our extracted `SCUS_942.28`**, so its
`src/` names and `asm/nonmatchings/` hold OUR addresses with no translation. It has the same role as
`open-spyro` — a hypothesis source, never evidence — and covers more of the game.

## Decompilation projects

| project | what it is |
|---|---|
| [TheMobyCollective/spyro-1](https://github.com/TheMobyCollective/spyro-1) | **Primary decomp reference** (CC0, submodule at `external/spyro-1`). Matching decompilation of SCUS_942.28; >40% C-matched, 242/270 listed functions checked. Same target checksum as our extraction, so `src/` names OUR addresses directly. |
| [theMagicalKarp/open-spyro](https://github.com/theMagicalKarp/open-spyro) | Byte-for-byte matching decompilation of SCUS_942.28 (CC0, submodule at `external/open-spyro`). Uses GCC 2.7.2 + maspsx + splat/spimdisasm; every commit rebuilds a byte-identical executable and stays runnable. Reports the game as the main EXE **plus 37 overlays**, ~828 functions (673 game, 155 PSY-Q/libc). **14.63% C-matched as of 2026-08-12** — this cell said ~5% and was stale by roughly 3x, which matters because the figure is how you judge whether a name you need is likely to exist yet. Verified the same day: our extracted `SCUS_942.28` is SHA-1 `84e3728ab94720d0873e2514adf4aade4935e0c5`, **byte-identical to its target**, so `config/symbol_addrs.txt` and `include/{types,funcs,globals}.h` name OUR addresses with no translation. |
| [celophi/spyro-decompilation](https://github.com/celophi/spyro-decompilation) | Decompilation to C using a different approach — recreated functions placed in extra RAM. |

## How these differ from this project

A **decompilation** recovers human-written source that recompiles to the original binary. This repo
is a **static recompilation port**: psxport translates the shipped MIPS machine code into C
automatically, runs it on a native PC platform layer, and native reimplementations then replace that
substrate function by function, each gated byte-exact against the code it replaces. The two are
complementary — a decomp is an excellent source of *function boundaries and names* to check our RE
against, and it answers structural questions (like the overlay count above) that are expensive to
derive from scratch.

## Open cross-check

The overlay question is currently **unresolved** — see [`issues/0001`](issues/0001-whether-spyro-loads-code-overlays-and-from-where.md).
The decomp projects say 37 overlays exist; the disc image contains no per-overlay files. Our recomp
covers only the resident executable today. This is exactly the kind of discrepancy a reference is
useful for surfacing and a running port is needed to settle.
