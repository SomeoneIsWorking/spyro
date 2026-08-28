---
id: 98
title: Spyro FIELD player model owner was unwired
status: resolved
symptom: controlled native FIELD gameplay moved Spyro but did not submit the player's model
tags: spyro1,render,field,actor,gameplay
created: 2026-08-29
updated: 2026-08-29
---

Affected state items: S005, S007.

## Root cause

FIELD called the regular Moby owner but never called the retail `0x80023AC4` player-model arm. The
existing paired-actor implementation was only reachable from stage-13 mode 3, where it used an
isolated painter policy. Adding that isolated object directly to FIELD would make the authored world
queue refuse at the later environment owner, because the framework correctly rejects mixed replay
policies in one world stream.

## Fix

`fx_field_player_actor.*` now owns the source-backed `g_IsSpyroHidden` gate at `0x80075814` and calls
the normal three-layer paired-actor decoder for visible FIELD frames. FIELD paired faces use a new
authored replay phase after regular and secondary actors; the stage-13 owner remains isolated. The
replay phase is tested with the existing scene-order contract and the hidden-value policy has a
focused test.

## Verification

`uv run --frozen python tools/verify.py --jobs 2` passed all 66 tests with Clang and the recorded
framework pin. The controlled native route in
`scratch/logs/field-player-authored-gate.log` exited 0 after the REPL end, rendered 875 FIELD
`0x80023AC4` groups over 1,697 native FIELD frames, completed 1,821 reconciled logic frames with zero
dropped layers, and emitted no native-render refusal. The existing temporal proof also remained
eligible on all 223 eligible intervals, with 446/446 callback emissions.

## Remaining scope

This resolves only the player model arm. Secondary/shaded actors, Moby and Spyro shadows, flame/trail
effects, glows/sparkles, alternate/status-plane and semi-transparent variants, whole-batch atomic
composition, and independent visual/oracle parity remain separate issues.
