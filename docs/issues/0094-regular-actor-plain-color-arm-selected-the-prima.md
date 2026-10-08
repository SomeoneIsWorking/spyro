---
id: 94
title: Regular actor Plain color arm selected the primary descriptor material pair
status: resolved
symptom: The first Artisans stage-0 regular actor recipe refuses atomically at record 0 with actor_prefix PlainColor
tags: render,field,actor,material,re
created: 2026-08-28
updated: 2026-08-28
---

## Root cause

Resident renderer `0x8001F798` branches after projection on CR30. The non-positive direct arm reaches `0x8001FE98` and selects the descriptor secondary pair: command pointer at `+28` and colour pointer at `+32`, with no DPCS scratch table. The native capture always selected the primary `+20`/`+24` pair and the pure prefix builder treated `Plain` as unsupported, so the first retained FIELD record refused before candidate decoding.

## Resolution and falsifiers

`actor_recipe_capture::descriptorMaterial` is now the one source of truth for the four binary arms: High `+20/+24` direct, PositiveBlend `+20/+24` scratch, Plain `+28/+32` direct, NegativeBlend `+28/+32` scratch. The prefix builder accepts Plain as an unmodified direct colour/command stream while NegativeBlend remains the opposite-answer refusal. Hermetic tests cover all four pair/scratch classes and prove Plain preserves its selected words without fog. On `scratch/raw/miss_ram.bin`, the exact regular scene remains 175 scanned / 14 queued and advances from a record-0 prefix refusal to Ready with 423 candidates, 211 rejects, and 212 faces. Stage 0 remains unwired; Moby-shadow production/rendering is the next actor-pass responsibility.
