---
id: C152
kind: claim
status: holds
created: 2026-08-05
tags: logging,lucent,framework
depends: external/psxport/cmake/psxport.cmake, external/psxport/tests/test_lucent_channel_env.cpp
---

## Claim

PSXPORT_DEBUG and PSXPORT_LOG_FILE are read by lucent itself (LUCENT_CHANNEL_ENV / LUCENT_LOG_FILE_ENV, set in cmake/psxport.cmake), resolved lazily on the first log call — so they work with NO cfg_* call having run first, and the cfg_* retirement can no longer switch every debug channel off.

## Evidence

A/B of one pre-main source against lucent HEAD f12c954 vs the patched lucent: 0 lines vs 1 line captured. psxport tests/test_lucent_channel_env.cpp RED at origin tip (0 lines captured, both cases), GREEN after (3/3 tests, 8 checks). Runtime: PSXPORT_DEBUG=cd,boot on the real port emitted 71 [cd] lines from CONVERTED lucent::debug sites; same binary with the var unset emitted 0.

## What would falsify it

a lucent::debug on a PSXPORT_DEBUG-named channel emitting nothing when it is the first log call in the process — re-run ctest -R test_lucent_channel_env; also falsified if cmake/psxport.cmake stops setting LUCENT_CHANNEL_ENV
