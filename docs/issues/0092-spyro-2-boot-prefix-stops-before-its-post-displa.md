---
id: 92
title: Spyro 2 boot prefix stops before its post-display initialization and loader chain
status: open
symptom: spyro2_port presents the three owned display-bootstrap fields, returns to 0x80011EB4, then deliberately aborts before binary-owned leaf 0x80011B1C
tags: spyro2,boot,loader,re,ownership
created: 2026-08-28
updated: 2026-08-28
---

Live PID 3564943 completed the measured three-field display bootstrap without reaching guest VSync and stopped at 0x80011B1C. The retained boot prefix next calls 0x80011B1C, 0x80011B3C, 0x80012B84, and 0x80011D24 before loader 0x80013810 and a later dispatch to 0x80077374 outside the resident executable text. The next work must identify and own these finite initialization leaves, then prove the loader's source/base and payload before dispatching the non-resident target. Do not bypass the boundary or seed 0x80077374 as resident code.
