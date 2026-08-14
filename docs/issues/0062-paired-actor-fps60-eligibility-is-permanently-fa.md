---
id: 62
title: Paired-actor FPS60 eligibility is permanently false under exact endpoint face identity
status: resolved
symptom: FPS60 runs at dual-present cadence but paired actor reports tier1=0 and does not interpolate
tags: fps60,paired-actor,lerp,nclip,measured
created: 2026-08-14
updated: 2026-08-14
---

Live native 4100-frame run scratch/logs/paired_fps60_current.log proved framebuffer/timing green (wrong_half=0/1830), but paired tier1 remained zero. Discriminator scratch/logs/paired_temporal_compat.log found 160/274 consecutive recipes compatible; scratch/logs/paired_temporal_faces.log found 0/160 identical resolved face sequences. NCLIP/quad split/OT acceptance changes every reached moving interval, so requiring exact endpoint ResolvedFace identity makes temporal ownership unreachable by construction. Do not weaken by silently choosing one endpoint face set. Root work is an evidence-backed continuous intermediate face resolver or another explicitly defined enhancement policy, with exact t=0/t=1 endpoints and live positive/negative denominators.

### Resolution (2026-08-14)
Root cause fixed by resolving the invariant source primitive stream continuously at strict interior t rather than comparing endpoint-cull outputs. Live paired_temporal_paced.log: 160/160 compatible midpoint resolves accepted and 320/320 temporal calls emitted; forced endpoints and audio-paced run exit 0; wrong_half=0/1830.
