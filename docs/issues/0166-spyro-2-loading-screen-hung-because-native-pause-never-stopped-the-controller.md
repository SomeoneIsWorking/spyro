---
id: 166
title: Spyro 2 hung on its loading screen because a native Pause left the CD controller reading
status: resolved
symptom: after New Game gamestate 5 never ended; 2.3 million Pause commands in the log
tags: spyro2,cd,controller
created: 2026-10-01
updated: 2026-10-01
---

## Answer

`cd_apply_command` handled Pause and Stop for the native stream state, but ReadN/ReadS had started the controller
reading (`cdc_begin_read`) and nothing stopped it or queued INT3/INT2. Spyro 2's XA state machine reads libcd's status
word after a Pause and saw "reading" forever, so it re-issued Pause. psxport `s23-play` commit `f720e6d4` adds
`cdc_issue_command`, which schedules the command through the controller's own command phase (INT3 with the old status
0x22, then INT2 with 0x02); `tests/test_cdc_continuous_read.cpp` has the positive and an idle-controller negative.

## Result

Spyro 2 reaches Glimmer gameplay (S024, issue 0167).
