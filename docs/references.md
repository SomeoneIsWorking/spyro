# External references

Public reverse-engineering work on Spyro the Dragon. **None of it is vendored, copied, or built
into this repo** — these are pointers for cross-checking our own RE (symbol names, function
boundaries, structure). Where a reference and our own measurement disagree, the measurement wins:
this repo's rule is that a claim cites evidence from the binary or a running port.

## Decompilation projects

| project | what it is |
|---|---|
| [theMagicalKarp/open-spyro](https://github.com/theMagicalKarp/open-spyro) | Byte-for-byte matching decompilation of SCUS_942.28. Uses GCC 2.7.2 + maspsx + splat/spimdisasm; every commit rebuilds a byte-identical executable and stays runnable. Reports the game as the main EXE **plus 37 overlays**, ~828 functions, ~5% C-matched. |
| [TheMobyCollective/spyro-1](https://github.com/TheMobyCollective/spyro-1) | Spyro the Dragon decompilation aiming at a matching executable. |
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
