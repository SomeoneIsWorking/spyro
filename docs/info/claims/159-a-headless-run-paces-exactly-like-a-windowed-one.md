---
id: C159
kind: claim
status: holds
created: 2026-08-06
tags: pacing,headless,parity,framework,ires
depends: external/psxport/runtime/recomp/pace_plan.h, external/psxport/runtime/recomp/video_plan.h,
  external/psxport/runtime/recomp/gpu_native.cpp
# NOTE: these live in the SUBMODULE. `claim check` runs its rot query against THIS repo, where a
# submodule is one gitlink — so a change inside psxport shows up only as the gitlink moving. That is
# a coarse signal, not a false one: read it as "the framework moved, re-check", not "pace_plan.h
# changed". At the time of writing the patch is UNCOMMITTED (claim pace-parity), so the gitlink has
# not moved at all and the check cannot see this claim's code yet.
---

## Claim

A headless run PACES exactly like a windowed one, at the game's REAL field rate (NTSC 59.940 Hz decoded from GP1(08) bit 3), and the AUTO internal-resolution scale is derived from the presentation SINK rather than from a window that headless does not have. Headless = no window surface + no audio device, nothing else.

## Evidence

psxport (uncommitted, claim pace-parity). MEASURED on spyro, one build, headless only (PSXPORT_VK_HEADLESS=1 PSXPORT_NOAUDIO=1), 60 s each leg on ONE PINNED binary (scratch/bin/spyro_port.paceparity, md5 d5cc32b9), instrument = spyro's own PSXPORT_DEBUG=pace counters. PACED: 3577 presents in 59.747 s = 59.87 Hz -- the game's own field rate, 59.940 Hz, to within 0.12%. UNPACED (PSXPORT_NOPACE=1, the ONLY difference between the two legs): 26888 presents in 59.861 s = 449 Hz. Boot-progress gate, same binary lineage, headless + PSXPORT_NOPACE=1, 900 s: 338024 presents, 0 aborts, 0 recomp misses (the recorded pre-change baseline was 212334). Milestone sets are IDENTICAL between the paced and unpaced legs (boot/disc/display-depth lines), i.e. pacing changes WHEN a frame happens, never WHAT happens. PSXPORT_DEBUG=pacer over 1167 calls: interval=16.6834 ms, rate=59940 mHz, 7 resyncs -- i.e. one NTSC field, not the 16.6667 ms a 60.000 Hz literal gives. Decoded standard logged once per run: '[gpu] display standard -> NTSC (59.940 Hz fields, GP1(08)=08000012)'. IRES, same headless mode both ways (NEGATIVE CONTROL): AUTO ires with the default 960x720 sink builds 'ires targets 3072x1536 (scale=3)'; the SAME run with PSXPORT_PRESENT_SINK=320x240 -- exactly the win_h() fallback the old code used when there was no window -- builds no ires target at all (scale 1), which is the failing answer. Hermetic: psxport tests/test_pace_plan.cpp 13 cases/105 checks and tests/test_video_plan.cpp 11 cases/63 checks, each carrying a transcription of the shipped rule as its negative control (RED before: 4/11 and 8/10 passing). Full framework suite 21/21.

## What would falsify it

if gpu_pace_subframe or the ires derivation ever regains an input that depends on whether a window exists (grep for gpu_has_window / win_w / win_h outside sink_size), or if a game is added whose field rate is not what GP1(08) bit 3 says
