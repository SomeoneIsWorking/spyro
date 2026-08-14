---
id: 63
title: Spyro FPS60 video crawls while audio runs ahead
status: resolved
symptom: game audio reaches later scenes while synthesized FPS60 video is still crawling behind
tags: fps60,pacing,audio,video,spyro,framework
created: 2026-08-14
updated: 2026-08-14
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-14)
Root cause: nativeFrameEnd delegated two guest VBlank/audio fields to one FPS60 logic commit, but generic frame_commit inherited Spyro paceQuota=1 and split only one field across its two presents. The game/audio clock therefore advanced about twice real time (~108 VBlanks/s measured) while rendering lagged. Framework frame_commit now accepts the explicit guest-field count; Spyro passes 2, producing two ~16.68 ms present intervals. The final audio-enabled terrain run `scratch/logs/terrain_fps60_audio_paced.log` reached VBlank 438 at 7.2594 s and 1000 at 16.6085 s: 562 fields in 9.3491 s (60.11 Hz), 281 commits, `wrong_half=0/281`, terrain active at roughly 490--577 faces per logic frame, and no refusal/fatal. Audible alignment still requires operator observation because the log proves clock and mixer-enabled execution, not what reached the speakers.
