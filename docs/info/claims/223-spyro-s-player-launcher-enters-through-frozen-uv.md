---
id: C223
kind: claim
status: holds
created: 2026-08-24
tags: 
depends: run.sh, bootstrap.py, tools/run.py#configure, tools/run.py#compiler_arguments, tools/run.py#execute, tests/test_launcher.py
---

## Claim

Spyro's player launcher enters through frozen uv and propagates that Python into both CMake builds without restricting compiler identity or invoking the test suite

## Evidence

2026-08-24: direct `uv run --frozen python bootstrap.py --help` completed through the locked bootstrap without provisioning or launch. `uv run --frozen python tests/test_launcher.py` passed 13/13 hermetic tests covering zero-argument target routing, isolated `scratch/build/player`, port-only target selection, prepare-only no-launch, default BUILD_TESTING=OFF, explicit maintainer-only BUILD_TESTING=ON, locked Python3_EXECUTABLE, exact Fedora/APT/macOS dependency refusals, explicit custom compiler forwarding, and CMake discovery when Clang is absent. `uv run --frozen python tests/test_provision_titles.py` passed 9/9. The real direct `CC=clang CXX=clang++ uv run --frozen python bootstrap.py --prepare-only` path matched the user-supplied Spyro 1 executable on all 11 identity facts, found the generated substrate current, and linked `scratch/bin/spyro_port` from isolated `scratch/build/player`; its cache records `BUILD_TESTING=OFF`. The separate `CC=clang CXX=clang++ uv run --frozen python tools/verify.py` maintainer path confirmed Clang, passed 30/30 CTests, and passed the psxport `9c2e3f1c` pin gate. No game/window was started.

## What would falsify it

run.sh stops using uv run --frozen, a Python child or CMake configure can escape sys.executable, the player path invokes CTest/test targets, or compiler acceptance depends on matching/rejecting an identity string
