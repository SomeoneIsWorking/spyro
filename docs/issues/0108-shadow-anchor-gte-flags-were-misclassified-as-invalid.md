---
id: 108
title: Level-entry Spyro shadows were refused when the retail GTE anchor saturated
status: resolved
symptom: The native field shadow producer refused a level-entry frame after the portal route even though retail still emitted the shadow fan
tags: render,shadow,gte,field
created: 2026-09-12
updated: 2026-09-12
---

## Cause

`0x80059A48` reads the GTE's projected SXY2/SZ3 and MAC1-3 values and does not branch on the
`FLAG` register. The native recipe treated any anchor error flag as an invalid projection, so the
first frames after an entrance sweep were dropped. The error bits describe saturation; they do not
make the source producer skip its fan.

## Fix and evidence

`field_shadow_recipe::derive` now refuses only when the owned scene projection was never published
or has no focal distance. It retains the anchor flags for diagnostics and uses the saturated
projected values exactly as the producer does. The focused test mutates the anchor into the
saturating case and requires all 16 fan faces plus the expected error bits.

The real route
`tools/drive.py gameplay --gate-teleport 0:0 --seek-portal --skip-transitions --after 300`
was rebuilt with Clang/Ninja and exited 0 after reaching the destination level. It rendered 300
post-entry fields with `fieldshadow PASS faces=16`, no native-render refusal, and zero Lightrec
fallback blocks or instructions.

## Related

- 0107 — the stage-1 transition producer that exposed this next boundary
