---
id: 78
title: Cached Spyro 1 executable bypassed selected-disc identity
status: resolved
symptom: Selecting a Spyro 2 disc could return early from the existing SCUS_942.28 cache without reading SYSTEM.CNF
tags: provisioning,identity,serial,spyro2,cache
created: 2026-08-22
updated: 2026-08-22
---

Root cause: tools/ensure_recomp.py treated the existence of scratch/bin/spyro/SCUS_942.28 as sufficient provisioning and returned before inspecting the selected disc. The cache key therefore encoded only the old target, not the selected media identity. Resolution: tools/provision_title.py now stages SYSTEM.CNF and its boot executable for the explicitly requested title, verifies the serial-specific executable manifest, and only then publishes to that title's cache. tests/test_provision_titles.py preloads a Spyro 1 cache and proves both answers: a staged SCUS_944.25 publishes only to Spyro 2, while a selected SCUS_942.28 disc is refused for Spyro 2 without replacing existing cache. Real Spyro 1 media re-provisioned with 11/11 identity facts matching. Spyro 2 disc provenance remains unmeasured because no Spyro 2 CHD was available; its separately supplied executable identity is measured, not its disc.
