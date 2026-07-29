---
id: I032
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

REPL 'shot' / gpu_native_shot — captures [s_disp_x,s_disp_y], which in this port is NOT where the finished frame is. Use REPL 'vram <path>' (full 1024x512) instead when you need to know what the game actually drew, and pick an ODD frame (even frames submit 0 prims).

## Validated by

Cross-checked against PSXPORT_PRIMDUMP=46501 (1609 polys with sane bboxes) and against a full-VRAM dump at the same frame, which showed a complete game image at (0,0) while 'shot' returned a two-colour flat fill from (0,240). The disagreement between the two is what exposed it: 'shot' was reporting a fresh clear as the presented frame. It fails SILENTLY — a flat capture looks identical to a renderer that draws nothing, and it cost most of a session's reasoning on issue 0035.

## Known failure modes

(none recorded yet)
