---
id: 44
title: The codemap's STATUS column rotted while every mechanical check on it stayed green
status: resolved
symptom: docs/codemap.md tells an agent the SCE/Universal logo screens are BLACK by deliberate framework design (C099) and that the gate is RED at frame 3781 (issue 0015) — both false; C099 is falsified and 0015 is resolved. codemap.py check reported no problems throughout.
tags: codemap,workflow,navigation,gpu
created: 2026-08-05
updated: 2026-08-05
---

## Why this mattered more than a doc nit

The codemap is the instrument the next debugging session NAVIGATES BY (USER, 2026-08-05: 'the code map
should be pristine so the agent knows where to debug from the code map'). Its top defect was the exact
failure the rule exists to prevent: C099's own falsification note says acting on it 'would have meant
porting a native producer for the logo screens to work around a one-line framework regression', and
the codemap still told an agent to do exactly that.

The map was not updated in the commit that changed the subsystem: `git log -- docs/codemap.md` stops
at 9810179, BEFORE the fixing commit 0408bdf.

## What was wrong (all verified before editing)

S1  Rendering section asserted C099 (`status: falsified`, 2026-08-04) as live. Replaced with C149 +
    issue 0043 + I037, and the full three-times-black history (0029/C104, 0016/C105, 0043/C149).
S2  Gate row said '10 checks, currently RED, port aborts at frame 3781 (issue 0015)'. Issue 0015 is
    resolved; there are 13 chk sites (16 checks total, now 17); measured 17/17 PASS at 56602 frames.
S3  A ~~strikethrough~~ TOMBSTONE row contradicted the row three lines above it. Deleted per
    no-tombstones, folding in the surviving cdReadPrim/cdFileLoad/cdAsyncRead = 0 detail.
S4  That same row was a MALFORMED markdown table row — a 5th cell past a 4-column header — so a live
    correctness warning ('wiring cdReadPrim to a (mode, buf) function would corrupt guest memory')
    rendered INVISIBLE in every renderer. Merged into cell 4.
S5  Six files absent from the map: native_angle/gte/render/terrain/util.cpp and spyro_game.h.
S6  Issue 0016 cited as live; resolved by C105 (24bpp display depth).
S7  Two Performance bullets contradicted each other on cfg_dbg_generation. Truth: it is OFF the hot
    path — OtAttr::trackStore is now an inline lucent::Channel test — and cfg_dbg_generation() survives
    only as a dead declaration of the retired cfg_* shim (psxport de187614).
S8  '7 overlays extracted'; game/overlays.json holds 12. Note a gate RUN identifies 7 — that is the
    per-run count, not a discrepancy, and the map now says so.
S9  external/open-spyro/, native per-vertex depth, seven tools/*.py, frame pacing/paceQuota and FMV
    had no rows. `codemap.py check` existed, was RED (external/open-spyro/), and was wired to nothing.
S10 re_frontier.py referenced game/core/level_load_probe.cpp, deleted long ago.

Also fixed, found while verifying: GP1(08) bit 4 described as 'decoded but NOT honoured' (it is
honoured, C105); gate.sh's own header said '20 of 21 overrides super-call ... genuinely native: the
vblank wait and rand()' (it is 35 overrides / 15 native bodies); spyro_game.h had a duplicate
declaration and an orphaned TEMPORARY comment for a deleted file; instrument I038 pointed at
scratch/logsig.py — a GITIGNORED path no clone or subagent could open (moved to tools/logsig.py).

## The systemic finding

NOTHING gated codemap drift in any of the three repos. codemap.py check is now wired into
tools/gate.sh, validated in both directions (I039), and prints its denominator AND its blind spots on
every run — because it was green through all of the above and would be again. It sees dangling
references only; it cannot see a wrong status, a falsified claim, a resolved issue or a stale count.
Deliberately NOT a git hook: the user had the only pre-commit hook removed on 2026-08-04.
