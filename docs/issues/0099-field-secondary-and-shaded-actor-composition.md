---
id: 99
title: Spyro FIELD secondary and shaded actors were not composed
status: resolved
symptom: native FIELD rendered the regular actor and player but omitted secondary and shaded actor passes
tags: spyro1,render,field,actor,transaction
created: 2026-08-29
updated: 2026-08-29
---

Affected state items: S005, S007.

## Root cause

FIELD's native seam called the regular actor owner and the player owner but never called the retail
`0x80020F34` secondary pass or `0x80022A2C` shaded queue. The two existing owners were individually
ready, but both consume and advance the shared shadow cursor at `0x80075F00`; calling them as
independent layers would permit one owner to commit state before the other refused.

The secondary source list also depends on the state-only retail `0x800521C0` list builder. The regular
native owner reads the level Moby array directly, so the missing builder was invisible until the
secondary owner was wired and the first live FIELD frame refused its uninitialized list.

## Fix

`fx_field_actor_composition.*` now prepares the secondary and shaded scenes, derives both recipes,
and uses the framework's batch painter admission before committing either scene. It rebases the
shaded shadow output after the secondary output, then publishes the two queue objects in retail
order. FIELD now calls the state-only `0x800521C0` builder before collectables and actors, matching
the source draw order.

This is deliberately scoped to the two actor objects. The regular actor and player owners still have
their existing adjacent publication boundaries, and Moby/Spyro shadow packets plus flame, glow, and
sparkle effects remain separate work.

## Verification

The current Clang product build passed. A real native headless replay with the left-control session
ran `3,700` presented fields and exited through the REPL with no native-render refusal, `1,910`
reconciled logic frames, and repeated `fieldactors PASS` records. The live actor composition emitted
roughly `110`–`120` shaded faces per FIELD frame after the list builder ran; the secondary pass was
valid-empty on that route. The prior refusal at the first FIELD frame was reproduced before the
builder call and disappeared after it.

The framework batch admission has focused tests proving multi-object capacity, replay-policy,
duplicate-object, and no-mutation behavior. Full gate and oracle comparison remain required before
calling this complete visual parity.
