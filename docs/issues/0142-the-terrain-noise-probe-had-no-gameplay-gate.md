---
id: 0142
title: The terrain-noise probe had no gameplay gate and published a terrain verdict from a boot logo
status: open
symptom: reported "16:9 legs ground 14.3% vs buildings 19.9%
  -- coherent" for the frame it captured, and the four-leg cross that verdict belongs to was
  recorded as evidence about the pool-water defect. The captured frame was the UNIVERSAL
  INTERACTIVE boot logo.
tags: instrument,diagnostics,terrain,noise,water,refusal
created: 2026-09-28
updated: 2026-09-28
---

## What the instrument actually measured

Re-run on 2026-09-28 at its own default of 260 fields, all four legs produced **byte-identical
figures** (4:3 `ground 8.5% buildings 3.7%`, 16:9 `ground 14.3% buildings 19.9%`) — and the
capture, opened as an image, is **the UNIVERSAL INTERACTIVE boot logo**.

A boot logo has no ground and no buildings. Both "regions" were scenery, and the tool reported a
verdict about terrain texture noise from a frame containing no terrain. **The four-leg cross the
tool exists to perform — separating the projection from the temporal pass — was computed over four
copies of a logo**, and its conclusion ("neither enhancement causes the noise") rested on that.

**This is the workspace's recorded failure mode in its purest form: an instrument returning a
confident number about the wrong thing.** reported "0 primitives carried depth" for
counters it never reached; a GTE reader returned garbage with no denominator; the boot-audit printed
an undeclared knob. Here the shape is a *plausible ratio* — 14.3 vs 19.9 is within a factor of two
and reads as a careful measurement — computed from scenery.

## The route was the defect, and the first fix was ALSO wrong

The tool reached the screen with `port.run(260)`. **A fixed frame count is not a route to
gameplay**, and `drive.py` exists precisely because of that: its own docstring records that the same
fixed script reaches the save picker on one run and the Insomniac card on the next. The attract
sequence is timing dependent, so 260 fields lands somewhere different every time — and on 2026-09-28
it landed on a logo both times the tool was run today.

**The first repair was a refusal gated on `g_Gamestate == 0`, and it did not fire.** Not because the
read was wrong but because **`g_Gamestate` is 0 during the boot logos too** — the recorded boot
timeline has both logos at gamestate 0, gameplay at 0, and the title screen at 13. So
"gamestate == GS_Playing" **is not a gameplay predicate on this title**, and a gate built on it would
have been a gate that cannot fail on the exact input it exists to catch. That is worth recording
separately: the obvious one-line gate was wrong, and only running it revealed that.

**The repair that holds** uses `drive.Navigator.reach_gameplay()`, which OBSERVES the guest — title
menu, start new game, wait for playing — and refuses by name when a state is not reached. The leg
then re-reads `g_Gamestate` and still refuses if the state moved away, naming the state it found.

## The figures after the fix are entirely different, which is the proof

| leg | before (a logo) | after (gameplay) |
|---|---|---|
| 4:3-nointerp | ground 8.5% / buildings 3.7% | **ground 22.2% / buildings 59.0%** |
| 4:3-interp | ground 8.5% / buildings 3.7% | **ground 22.2% / buildings 59.0%** |
| 16:9-nointerp | ground 14.3% / buildings 19.9% | **ground 26.8% / buildings 51.0%** |
| 16:9-interp | ground 14.3% / buildings 19.9% | **ground 26.8% / buildings 51.0%** |

**Nothing about the previous numbers survives**, which is the only honest way to show the old verdict
was not a weak result but a result about the wrong frame.

**The new numbers still do not settle the pool.** The verdict rule is a factor-of-two comparison
between two regions, and the tool says so itself — but 22.2% vs 59.0% is a ratio of 2.7, so the
4:3 legs read "coherent" only because the buildings are *noisier* than the ground, which is not a
statement about the ground at all. **The factor-of-two threshold is arbitrary and this measurement
does not resolve the defect.** The pool is measured by instead, and that
is the instrument to read.

## What the pool probe found when pointed at the arrival route

 **REFUSED** every frame the gameplay route produced at settle 60, 140 and 260
and after holding each of down/left/right: `no connected saturated-blue region of at least 2500 px
and at least 0.45 box aspect. Largest found: 464 to 1465 px.` **The pool is not visible on the
arrival route.** The captured frame shows Spyro in the Artisans courtyard facing the castle gate with
no water in frame, and the reference defect frame faces the pool with the castle *behind* it.

**So the water fix is still unverified on screen**, and this is the concrete reason: reaching the
pool needs a walk to it that no maintained route currently expresses, not a longer settle and not a
different direction held for longer. Six routes were tried and all six refused.

**Falsifier for this issue:** a `drive.py` route that reaches a frame the pool probe measures
(`>= 2500` connected blue px, box aspect `>= 0.45`) closes it, and the same frame measured on both
sides of the fix is what closes the water defect itself.
