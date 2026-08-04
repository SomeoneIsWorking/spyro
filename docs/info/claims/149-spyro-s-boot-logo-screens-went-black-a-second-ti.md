---
id: C149
kind: claim
status: holds
created: 2026-08-04
tags: gpu,framework,regression
---

## Claim

Spyro's boot logo screens went black a second time because present()'s empty-batch early-out (afca817d) sits ABOVE upload_vram — an upload-only screen is a NEW picture with zero primitives, and the geometry batch alone cannot tell those apart

## Evidence

MEASURED before/after on the same binary path, PSXPORT_NOWINDOW=1 PSXPORT_NOAUDIO=1 PSXPORT_SHOT_AT=30,60,120,200,300,420,600,900. BEFORE: presents 30/60/120/200/300/420 all 0.0% non-black, 1 colour; 600/900 real (93.3%, 2117/2120 colours). AFTER: 30/60/120/200 = 2.7% non-black / 252 colours (the SCE 'SONY COMPUTER ENTERTAINMENT AMERICA presents' card, read as a PNG and confirmed legible), 300/420 = 27.2% / 16216 colours (the Universal Interactive Studios globe, full 512 width, correct colour), 600/900 unchanged at 93.3% / 2117 / 2120 — the 3D scene is byte-for-byte unaffected. tools/gate.sh 90: 16/16 PASS, 49817 frames, 0 divergences, 0 recomp misses. Mechanism located by git log -L on present() line 931: afca817d 'gpu: a present with no new geometry re-shows the last composite'. New 'debug presentskip' instrument tallies the decision with its denominator: over 84400 presents Spyro takes reuse_last 404, rebuild_vram 33, rest rebuild_geom — both non-geometry counts plateau by present ~600, so the change touches ONLY the boot logo phase and afca817d's idle-field saving is fully retained (192/200 of the first 200 presents still reuse).

## What would falsify it

if a port with preserveVramBackdrop=1 shows alternating black at 30Hz again, the guest is writing VRAM on its idle field and the coarse write COUNT is too blunt a change signal — it would then need to discriminate writes landing in a framebuffer from texture/CLUT uploads
