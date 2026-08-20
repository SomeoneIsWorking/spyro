---
id: C208
kind: claim
status: holds
created: 2026-08-21
tags:
depends: tools/run.py#execute, tools/run.py#configure, run.sh
reconfirmed: 2026-08-21 01:39:15
verified_at: 2026-08-21 01:39:15
---

## Claim

Spyro's zero-argument launcher provisions and starts the native spyro_port target through a retained Clang build cache

## Evidence

2026-08-21: ./run.sh with no disc argument resolved the configured disc, built/provisioned, logged render path = native, and exited rc=0 at PSXPORT_NATIVE_FRAMES=400. After the Clang selection marker was established, two consecutive no-argument runs retained both caches; the second reported recomp up to date and exited rc=0 in 2.1 seconds without reconfiguring or recompiling. An explicit nonexistent disc exited 1 before any CMake build or launch. CTest launcher covers the shipping sequence, target paths, refusal ordering, and environment contract.

## What would falsify it

A no-argument ./run.sh selects a non-native/non-spyro_port target, fails with a valid configured disc, or performs a fresh configure/recomp on an unchanged compatible cache.

## Re-confirmed 2026-08-21 01:39:15

2026-08-21 final implementation: true no-argument launcher reached native spyro_port and exited rc=0 at 400 fields; explicit missing disc refused rc=1 before CMake; after the stable Clang selection marker was installed, consecutive capped no-argument runs retained both build trees and the unchanged second run exited rc=0 in 2.1 seconds without reconfigure/recomp; full corrected-Clang CTest passed 8/8.
