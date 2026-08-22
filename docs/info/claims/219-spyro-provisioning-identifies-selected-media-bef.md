---
id: C219
kind: claim
status: holds
created: 2026-08-22
tags: provisioning,identity,spyro2
depends: tools/provision_title.py#provision, tools/ensure_recomp.py#extract_exe, tests/test_provision_titles.py
---

## Claim

Spyro provisioning identifies selected media before publishing or reusing a title cache

## Evidence

Issue 0078 names the old early-return root cause. tools/provision_title.py always extracts SYSTEM.CNF and the boot executable into fresh scratch staging, compares the boot target to the requested title manifest, verifies 11 identity facts, and only then os.replace publishes. The seven-case provisioning/identity CTest proves both title and refusal answers with a pre-existing cache; real Spyro 1 CHD provisioning matched 11/11.

## What would falsify it

a provisioning path can return an existing executable before staging SYSTEM.CNF from the currently selected disc, or a requested title accepts another serial
