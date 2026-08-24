---
id: 81
title: Launcher refactor breaks the maintainer verifier before tests start
status: resolved
symptom: tools/verify.py aborts with AttributeError: module run has no attribute BUILD after player launcher isolation
tags: launcher,verification,build,ctest
created: 2026-08-24
updated: 2026-08-24
---

## Root cause

The player launcher refactor removed the shared `BUILD` constant and changed `configure` from
separate compiler arguments to one compiler-option vector. The explicit maintainer verifier still
called both old APIs. The refactor also made `BUILD_TESTING=OFF` unconditional, so mechanically
changing only the names would have configured a verifier with no tests.

## Resolution

Player and maintainer policy now share one configure implementation with explicit ownership:
`scratch/build/player` and `scratch/build/player-tools` default to `BUILD_TESTING=OFF`, while
`tools/verify.py` alone opts the `build/` maintainer tree into `BUILD_TESTING=ON` and mechanically
verifies `CMAKE_CXX_COMPILER_ID=Clang`. The shipping path builds only `spyro_port` and never invokes
CTest. Verified with the real non-launching player build plus the explicit maintainer gate: 30/30
CTests and the psxport pin check passed.
