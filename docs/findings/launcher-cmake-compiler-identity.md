# The launcher must hand CMake the compiler path CMake will record

`./run.sh` recompiled the whole port on every run and printed CMake's cache-deletion banner:

```
You have changed variables that require your cache to be deleted.
The following variables have changed:
CMAKE_C_COMPILER= clang
CMAKE_C_COMPILER= clang
CMAKE_C_COMPILER= clang
CMAKE_C_COMPILER= clang
```

That banner is not a warning about a preference. It is CMake discarding a build tree, and the reason
it never stopped was a mismatch the launcher created on every run.

## What was measured

- `objects_total=489`, `rebuilt_in_last_12min=489`: one `./run.sh` rebuilt 100% of the port.
- `build/player/CMakeCache.txt` recorded `CMAKE_CXX_COMPILER:FILEPATH=/usr/lib64/ccache/c++`.
- `which -a clang` lists `/usr/lib64/ccache/clang` **before** `/usr/bin/clang`; `/usr/lib64/ccache/*`
  are symlinks to `/usr/bin/ccache`, which dispatches on `argv[0]`.
- Reproduced on the real source tree: a cache holding a resolved path plus a configure handed the bare
  name prints the banner (one message; the same variable appears 3 times for `CMAKE_CXX_COMPILER` and
  4 times for `CMAKE_C_COMPILER` in that run — the counts are CMake's and nothing here depends on
  them). Repeating the *same* configure reproduces it again, so it never converged.
- The banner is written to **stderr** (298 bytes stderr vs 1754 bytes stdout for the same configure),
  and `tools/run.py`'s `command(..., quiet=True)` redirects only stdout — which is why the message
  reached the terminal while CMake's ordinary configure output did not.

## Why it happened, and why it never stopped

`compiler_arguments()` passed the bare tokens `clang` / `clang++`. CMake does not store the token it is
handed; it stores the executable it found, and it compares that cache entry against the incoming `-D`
token on every later configure. Here the found executable is the ccache shim, so the cache and the
launcher's token disagreed as text on every single run.

CMake's answer to a disagreement is to delete its cache and re-run configure, and **that re-run does
not put the requested compiler back**. Measured on the real trees, the post-deletion cache recorded
`CMAKE_C_COMPILER = /usr/lib64/ccache/cc` in both `build/player` and `build/player-tools` — i.e. the C
half of the product was built by GNU `cc` while the launcher asked for Clang — and one reproduction
ended with `-- The CXX compiler identification is GNU 16.2.1`. Each deletion therefore landed on a
*different compiler executable* (Clang and GNU alternating), which changes every compile command and
makes Ninja rebuild every object: 489/489. The value left in the cache still did not equal the token
the launcher passed, so the next run repeated all of it.

The parent tree `build/` shows the other half of the rule: its cache held the bare names `clang` /
`clang++`, and handing it the paths those names resolve to rebuilt nothing, because CMake had been
executing the same path all along. Only a different compiler costs a rebuild; a different spelling of
the same one costs nothing.

The tree was therefore wrong in three ways at once, and only the first was visible: a full rebuild, a
silently substituted compiler, and a loop with no fixed point.

## The rules

**A launcher hands the build system the value the build system will record.** `resolved_compiler()`
resolves a chosen compiler through the host's `PATH` before passing it, so a warm tree agrees with its
cache. This keeps ccache in the driver chain — the resolved path *is* the ccache shim — and costs
nothing when the user names a path already.

**A different compiler is a real transition, so the launcher owns it explicitly.** `configure()` now
clears the CMake state itself when the recorded compiler differs from the requested one, instead of
leaving CMake to delete its cache mid-run and drop the request. A reset is what a compiler change costs
either way; doing it here is what keeps the requested compiler in the cache afterwards.

**Verify the compiler that was actually configured, and refuse rather than assume.** After
configuring, `compiler_mismatch()` reads the cache back and the launcher raises `Refusal` naming both
compilers. The failure it guards against is silent by construction: the artifact links, runs, and was
produced by a compiler nobody selected.

**Compare compiler identity, not spelling.** `same_compiler()` requires two absolute paths to be equal,
and compares basenames when either side is a bare name — the shape a hand-written cache holds. A
resolved path is deliberately **not** symlink-resolved: ccache dispatches on `argv[0]`, so
`/usr/lib64/ccache/clang` and `/usr/lib64/ccache/c++` are different compilers even though both resolve
to `/usr/bin/ccache`, and collapsing them would hide a real switch behind an alias.

## Verified

- `tests/test_launcher.py` (24 tests) covers alias spelling preserving the tree, a changed compiler
  resetting it, and a configure that records another compiler refusing by name.
- After one transition run, both real trees record the requested paths: `build/player` and
  `build/player-tools` hold `CMAKE_C_COMPILER=/usr/lib64/ccache/clang` and
  `CMAKE_CXX_COMPILER=/usr/lib64/ccache/clang++`. The transition run rebuilt once — 507 Ninja edges —
  which is what a genuine compiler change costs.
- Two consecutive no-change `uv run --frozen python tools/run.py --prepare-only` runs took 3 s and 2 s
  and produced **zero** `Building (C|CXX) object` lines. Before the fix the same command rebuilt
  489/489 objects.

## What a warm run still invokes

A warm run is not silent: it re-runs `[0/2] Re-checking globbed directories` and two generator edges —
`gen_gpu_shaders.py` and the framework's `stamping build identity`. The shader generator is an
`add_custom_target`, and CMake always considers a custom target out of date; psxport's comment says the
guard exists because Makefile generators do not rebuild a missing `BYPRODUCT` from a current stamp,
and the generator replaces the header only when its bytes change, so no compile follows. This launcher
requires Ninja, so that guard is dead weight here, but removing it changes a shared framework for every
consumer and belongs to a psxport change with its own verification — recorded rather than done in this
fix.
