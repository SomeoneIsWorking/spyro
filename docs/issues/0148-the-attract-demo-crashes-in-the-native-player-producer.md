---
id: 0148
title: The attract demo crashed in the native player producer because an override lost an immediate's sign
status: fixed
symptom: Left idle, the product plays the attract demo and dies after about 3 minutes with
  `FATAL: UNMAPPED RAM read8 @ 0x01EEDE6C`, deterministically, with or without any instrument armed.
state_items: S011
tags: crash,render,producer,attract,repro
created: 2026-09-29
updated: 2026-09-29
---

## Reproduction

`uv run --frozen python tools/demo_run.py --timeout 420` exits 139 in the same place every time. It
reproduced with the reach recorder and the differential armed, and with nothing armed.

    FATAL: UNMAPPED RAM read8 @ 0x01EEDE6C
    Core::mem_r8 <- spyro::paired_actor::make_stream <- paired_actor::build_descs
      <- spyro_field_player_submit <- spyro_field_model_chain_submit <- SpyroRenderer::renderScene

`make_stream` (`game/render/actor/paired_actor_pose.cpp:168`) reads the model pointer
`table[anim] + 0x38` and then dereferences it. In the demo, that word is not a pointer. The last lines
before the fault are `[pairedactor] ownership gate: leg=native ... PASS`. No drive.py gameplay route
crashes; only the self-playing demo does.

This is not issue 0128's recorded cause (the particle producer refusing type 3). It is on the same
route a user reaches by leaving the title idle.

## Cause: a swarm-written override lost an immediate's sign

The renderer was right. It reads the same table the original `0x80023AC4` does, under the same
`g_IsSpyroHidden` condition. The bad animation number came from round 4's
`advance_body_animation_with_transitions` (0x8003CBB8, commit 427aece). It read the per-state default
animation from `0x8007C470`, but retail builds that address at 0x8003CC38 as
`lui $v0,0x8007; addiu $v0,$v0,-0x3B90`, which is **0x8006C470** (`spyro_StateDefaultAnimation`). The
override read level-overlay bytes as animation numbers. On the gated gameplay level those bytes happened
to be valid animations, so nothing crashed there. The attract demo's level produced one past the model's
animation pointers, and the renderer followed it into unmapped RAM.

The override differential passed this override at 17/17 on the gameplay route. It sampled the first
16 calls and every 64th after that, and none of those took the transition branch with a divergent
byte. Shadowing EVERY call on the attract demo (`PSXPORT_OVERRIDE_DIFF_EVERY=1`) catches it: 8 of 395
calls mismatch, each on exactly `0x80078A71` (`m_nextBodyAnimation`), for example original 02 against
native 06. So the instrument can give the other answer, and the per-job gate now shadows every call.

## Fix

- `kStateDefaultAnimation` is now `0x8006C470`.
- `tools/override_constants.py` now refuses any constant in an override module that no `lui` (alone or
  with its immediate) in that module's own retail functions computes. It needs no route: it decodes the
  functions from `SCUS_942.28`. It fails the pre-fix module on exactly `0x8007C470` and passes all 11
  modules after the fix (60 constants in total).
- `tools/native_override_gate.py` runs it on every job.
- The worker prompt now states the sign-extension rule.

## Verified

On main at 4d08ce2 (and pinned in 7735458), `tools/reach_corpus.py`'s attract-demo route runs its
full 420-second clock and exits 0, with no `FATAL`. Before the fix it died about 3 minutes in, on every
run. The differential run that armed only this override (every call shadowed) is the discriminator: 8 of
395 calls mismatched before the fix, all on `0x80078A71`.
