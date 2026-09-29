---
id: 0148
title: The attract demo crashes in the native player producer on a non-pointer model word
status: open
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

`make_stream` (`game/render/paired_actor_pose.cpp:168`) reads the model pointer
`table[anim] + 0x38` and then dereferences it. In the demo, that word is not a pointer. The last lines
before the fault are `[pairedactor] ownership gate: leg=native ... PASS`. No drive.py gameplay route
crashes; only the self-playing demo does.

This is not issue 0128's recorded cause (the particle producer refusing type 3). It is on the same
route a user reaches by leaving the title idle.

## Next

Find the guest condition under which the original draws Spyro's body with this lookup, and make the
native gate match it. A pointer-range check on the failing value would hide the defect.
