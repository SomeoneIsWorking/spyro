---
id: 95
title: Producer verifier lacked recipes for concrete field producer scopes
status: resolved
symptom: native gate refused legitimate screen fade, collectables, actor, world, and shaded field ProducerScope labels as unmeasured
tags: producers,verification,render,ownership
created: 2026-08-28
updated: 2026-08-28
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-28)
The verifier enumerated every ProducerScope but only had recipes for the original six direct sites; shared actor/world submitters hid caller labels behind parameters, while new fade, border, collectables, and shaded-queue producers had no image fingerprints. Producer identity now lives at concrete FX callers, and verify_producers.py derives all 12 unique producer keys from exact single-match MAIN/overlay fingerprints; the native gate and selftests pass.
