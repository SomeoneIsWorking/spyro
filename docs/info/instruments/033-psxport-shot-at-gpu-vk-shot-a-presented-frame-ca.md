---
id: I033
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

PSXPORT_SHOT_AT / gpu_vk_shot — a presented-frame capture that reads [s_disp_x,s_disp_y] and, in this port, returns a two-colour flat fill for scenes the game renders correctly.

## Validated by

Compared directly against the full-VRAM dump at the SAME frame on the SAME build: shot_46501.ppm has 2 distinct colours and 93.3% non-black (a flat fill), while REPL 'vram' at f46501 shows the complete attract-demo scene. So the picture exists and this capture does not show it. Same family as I032 — prefer REPL 'vram' (both buffers in one image, nothing to trust) for any question about what the game drew. Also note SHOT_AT lives in gpu_present, so it only fires for a consumer whose frame loop calls gpu_present rather than gpu_present_ex.

## Known failure modes

(none recorded yet)
