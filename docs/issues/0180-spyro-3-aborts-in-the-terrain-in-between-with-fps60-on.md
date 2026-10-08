---
id: 180
title: Spyro 3 aborts in the terrain in-between at field 802 of its boot with fps60 on
status: resolved
symptom: spyro_port on SCUS_944.67 aborts (SIGABRT) once the retail boot prefix has returned; the picker walk dies with it while Spyro 3's panel pre-rolls
tags: spyro3,terrain,fps60,terrsink,picker
created: 2026-10-08
updated: 2026-10-08
---

## Observed

`tools/shipping_settings.ini` (aspect=1, fps60=1), build of 2026-10-08, deterministic:

- `spyro_port scratch/assets/spyro3/SCUS_944.67` with `PSXPORT_NATIVE_FRAMES=1500` aborts at product step 6 of the
  frame loop (frame 802 in `FrameLoopShell::step`), last log line
  `[terrsink:error] refusing a 0-word packet at 0x801E3D20 in a 143360-byte arena: the tag's own word count is 0`.
- Backtrace: `Fps60::tier1Render` -> `spyro::TerrainWorldPass::reconstruct` -> `terrain_packet_sink.h` `std::abort()`.
- The same run with `tools/wide_only_control_settings.ini` (fps60 off) completes 1500 fields, rc 0. Spyro 1 and 2 pass
  1500 fields with fps60 on.
- The zero-argument picker with fps60 on dies the same way while panel 2 (Spyro 3) pre-rolls, so the selector cannot
  be used with the shipping settings.

## Cause

`terrain_packet_sink.h` `forEachPacket` refused a tag with word count 0. The re-split pass empties an
oversized primitive in place exactly that way (retail 80028504; the GPU walks through it) and chains its
pieces after it. Once the walk started at a bin's first-linked packet (fe2806a) it reached those emptied
primitives; the old head-only walk never had.

## Fix

The walk follows a zero-length tag without visiting it. `test_terrain_packet_sink`
`an_emptied_primitive_is_walked_through`; Spyro 3 with `shipping_settings.ini` runs 1,501 fields with rc 0,
and the picker walk with fps60 on goes panels -> `pick spyro2` -> `session return` -> `pick spyro1` with no
error lines.
