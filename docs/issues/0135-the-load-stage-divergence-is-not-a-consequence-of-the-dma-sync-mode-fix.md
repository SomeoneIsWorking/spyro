# 0135 — the `load_stage` divergence is NOT a consequence of the DMA sync-mode fix, and the suspect is refuted

`docs/project-state.md` names an open question and its prime suspect:

> The open question, named rather than guessed: whether `load_stage` 1-vs-2 is a REGRESSION from a
> framework change since 2026-09-26. The only commits touching load-relevant runtime code are `492adace`
> ... and `436c3762`. `492adace` is the prime suspect on mechanism ... **That is a suspect, not a
> conclusion**, and the experiment that settles it is running against a framework worktree at `492adace^`.

This is that experiment. **The answer is no.** The divergence is present identically at the framework
commit immediately *before* `492adace`, so it cannot be a consequence of it.

## The measurement

One route, `uv run --frozen python -u tools/oracle_compare.py --policy demo`, run against three
framework commits. Everything else held constant: the same spyro sources, the same provisioned
`scratch/assets/spyro1/SCUS_942.28`, the same BIOS, the same default `aspect=1, fps60=1` product
configuration, the same `CMAKE_BUILD_TYPE=Release`, and the same two `vendor/` submodules (the gitlinks
at `492adace^` are byte-identical to the dev clone's, and both vendor checkouts sit exactly on them, so
the vendors are held constant by construction rather than by assumption).

| arm | framework commit | date | `load_stage` native/console | `player.position` native/console |
|---|---|---|---|---|
| control (dev clone) | `7f537273` | 2026-09-27 18:39 | `01000000` / `02000000` | `b3d70200fd4a02004d520000` / `eed70200924c02004d520000` |
| **`492adace^`** | **`7e3ae28f`** | **2026-09-27 10:48** | **`01000000` / `02000000`** | **`b3d70200fd4a02004d520000` / `eed70200924c02004d520000`** |
| oldest buildable | `8432c9b4` | 2026-09-27 05:44 | `01000000` / `02000000` | `b3d70200fd4a02004d520000` / `eed70200924c02004d520000` |

All three exit 1 on the same two decisive ranges, and the other three checkpoints are unchanged:
`demo_playing` MATCH on all 13 decisive ranges, `gameplay[0]` MATCH on 400/400f.

**The three runs are byte-for-byte identical**, not merely equal on the two reported cells. With the
elapsed-time line normalised away, all three logs hash to
`5a55a1bb8272f043096c13b8cda70048bfa5d0af67fa488c28818d0f623764c6`. Every arrival frame agrees too:
native `f3549` / `f4320` / `f5120` / `f5720`, console `vb4766` / `vb5930` / `vb6730` / `vb7330`. A
framework change that altered a load loop's iteration count would move those, or at least one byte.

**The instrument can tell the two frameworks apart.** The worktree has no `runtime/psx/dma_linked_list.*`
and zero `syncMode`/`chainWords` references in `mem.cpp`; the dev clone has two references. The arms
differ in the code under test, and the output does not move.

## What this establishes, and what it does not

- **It REFUTES `492adace`, and `436c3762` with it.** The whole range `7e3ae28f..7f537273` is
  exonerated, because the divergence is identical at both ends of it. The DMA sync-mode fix is not
  implicated in `load_stage`, and the MADR/linked-list mechanism proposed for it is not the cause.
- **It does NOT establish a cause.** Three identical points on a line say the divergence is at least as
  old as `8432c9b4` (2026-09-27 05:44). They do not say what introduced it.
- **The regression window is bounded, and closed at the bottom by a build failure rather than a
  measurement.** Current spyro sources do not compile against a framework older than `8432c9b4`:
  `game/core/main.cpp:12` includes `store_observe.h` (added `0e730da6`, 2026-09-27 00:11) and
  `main.cpp:89` calls `store_observe_attach` (added `8432c9b4`, 05:44). Configures-and-fails at
  `0e730da6` and `0a0e454b`. This is the WORKSPACE.md incident class — a game tree that has moved past a
  framework can no longer be built against it — so the window below `8432c9b4` is **not testable from
  today's tree without editing source**, which this issue does not do.
- **Nothing in the spyro route changed.** `tools/oracle_spyro1_demo.py` has exactly one commit
  (`9ed5ce2`) and its `load_stage` decisive line is unchanged; the shared lens `tools/oracle_spyro1.py`
  last changed at `b5e6f23` (2026-09-20), before the claim. So the measurement's definition is constant
  across the whole period and the spyro repo is not the variable.

## A correction to how the disagreement is framed

`docs/project-state.md` says the 2026-09-26 measurement found "the level entry MATCHED on every decisive
range" and that this "no longer holds". **On the level entry it does still hold.** The divergence is at
`demo_level_load`, the mid-load barrier — a different checkpoint, and the current text already says so
("What no longer compares clean is the load state machine's step index at the mid-load barrier, which is
a different thing and was previously not reported"). `demo_playing`, which *is* the level entry, matches
on all 13 decisive ranges including `level_id`, in all three arms above.

So the open question is narrower than "did a framework change regress this": it is **whether
`demo_level_load`'s `load_stage` was ever reported as clean**, and the remaining candidate window is the
9 framework commits between `0a0e454b` (2026-09-26 22:48, the dev clone's HEAD when `9ed5ce2` landed at
23:01) and `8432c9b4`, of which the first four (`95b3dcb5`, `d23800d4`, `2da9319e`, `0e730da6`) cannot be
built from today's spyro sources. Closing that needs either a framework-side revert of one of the
`store_observe` commits or a spyro tree of the matching date — not more bisecting of the range already
exonerated.

## Reproduction

    git -C ~/repo/psx/psxport worktree add <repo>/scratch/wt/dma-base 492adace^
    ln -s ~/repo/psx/psxport/vendor/beetle-psx <repo>/scratch/wt/dma-base/vendor/beetle-psx
    ln -s ~/repo/psx/psxport/vendor/lucent      <repo>/scratch/wt/dma-base/vendor/lucent
    cd <repo> && CXX=clang++ cmake -S . -B build/dma-base \
        -DPSXPORT_DIR=<repo>/scratch/wt/dma-base -DCMAKE_BUILD_TYPE=Release
    CXX=clang++ cmake --build build/dma-base -j$(nproc)
    uv run --frozen python -u tools/oracle_compare.py --policy demo \
        --executable build/dma-base/bin/spyro_port

`build/dma-base/psxport_resolved.txt` names the framework the build actually used; read it before
believing any number from that build directory. `-DCMAKE_BUILD_TYPE=Release` is required — the tree
defaults to `RelWithDebInfo`, and a Release-vs-RelWithDebInfo comparison would have confounded the
result with the optimisation level.
