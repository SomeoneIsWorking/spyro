---
id: 130
title: The agent settings asked for ASPECT_AUTO, so widescreen was OFF in every "enhancements on" run
status: resolved
symptom: tools/shipping_settings.ini said aspect=3 (ASPECT_AUTO), which resolves to the SINK's aspect. An agent run is headless and has no wide sink, so it resolved to 4:3: the product announced `aspect=3 wide_engine=1 native_width=512 render_width=512` and captured 512x240, while the same run at aspect=1 announced render_width=684 and captured 684x240
state_items: S019
tags: oracle,widescreen,configuration,instrument
created: 2026-09-20
---

## The second false baseline of the same week, in a different disguise

The first was 2026-09-19: `PSXPORT_SETTINGS` unset does not mean "defaults", it means "whatever
untracked file sits in the working directory", so a whole body of evidence had been collected with
both enhancements silently ON. `tools/shipping_settings.ini` and psxport's `agent_environment`
refusal were the fix.

This is the same failure one level in. The file now reaches the product and the product honours it —
and the value asked for ASPECT_AUTO, which is a request to decide later from something an agent run
does not have. So the fix for "the enhancements were off" produced runs in which widescreen was
**still off**, while everything about them said it was on.

Measured 2026-09-20, same route, same settled state, only `aspect` changed:

| aspect | announced | captured |
|---|---|---|
| 3 (AUTO) | `wide_engine=1 native_width=512 render_width=512` | 512x240 |
| 1 (16:9) | `wide_engine=1 native_width=512 render_width=684` | 684x240 |

`wide_engine=1` in BOTH, which is exactly why psxport's `runtime/psx/picture_announce.h` says to
read `render_width` and never that flag. That header already recorded Tomba! 2 measuring the same
pair (320 vs 428) on 2026-09-19. The fact was written down, in the framework, and neither title's
settings file was checked against it.

## What this invalidates

**Spyro's "14/14 checkpoints byte-identical with the enhancements off versus on" was not a
widescreen measurement.** Both arms rendered 512 wide. The same applies to Tomba! 2's 34/34. Those
results still say something true about fps60, which is a real setting and was genuinely on; they say
nothing whatever about widescreen.

Corroborated by an older capture in this tree: `scratch/logs/drive_full.log`, from the run that
produced that evidence, announces `aspect=3 wide_engine=1 native_width=512 render_width=512`.

## The measurement, now that widescreen is actually on

`tools/oracle_compare.py --bios ../SCPH1001.BIN` with `aspect=1 fps60=1`, the product log announcing
`render_width=684`:

```
15 checkpoints, 0 decisive divergences, complete: True
product_settings: {'path': tools/shipping_settings.ini, 'exists': True,
                   'values': {'aspect': '1', 'fps60': '1'}}
```

So the guest state a real console produces is reproduced exactly while the product renders a 684-wide
picture. That is the widescreen non-invasiveness claim, evidenced for the first time.

## What it still does not say

Non-invasiveness is not correctness. That the simulation is unchanged says nothing about whether the
additional horizontal area contains the right geometry; that is S019's separate question and its
evidence remains the port's own 4:3-vs-16:9 pair. A console comparison of the widened picture is
possible in principle — widescreen is defined as an extension about the same centre, so the central
512 columns should match the reference and the side panels should carry real scene content — and
this issue does not do it.

## Fixed

Both `tools/shipping_settings.ini` files name `aspect=1` and say why AUTO cannot be used for an
agent gate. The deeper hole is that nothing CHECKS the announced geometry against what the settings
asked for: psxport's picture_announce exists precisely because "an enhancement that silently fails
to engage produces a run indistinguishable from one that engaged", and no gate reads its line.


## 2026-09-20 — widescreen render correctness, measured

The remaining item above is closed, and the console turned out to be the wrong reference for it. A
console is 4:3, so it has nothing to say about the extra area. The product is asked about ITSELF at
one settled state under two settings, which is enough because widescreen is *defined* as a
deterministic horizontal extension about the same centre: the central 512 columns must survive, and
the margins must contain scene.

`tools/widescreen_check.py`, over psxport's `oracle/widescreen.py`:

```
[widescreen] narrow: native picture: aspect=0 wide_engine=0 native_width=512 render_width=512
[widescreen] wide:   native picture: aspect=1 wide_engine=1 native_width=512 render_width=684
[widescreen] 512 -> 684 (+86 per side): EXTENDS
[widescreen]   centre: 2671/122880 a different COLOUR (2.17%, tolerance 10%) — survived
[widescreen]   left margin:  93.3% non-black, 582 colours, 0/85 repeated columns — scene
[widescreen]   right margin: 93.3% non-black, 579 colours, 0/85 repeated columns — scene
```

Both runs announce their geometry and the tool REFUSES if either does not, so an aspect that failed
to engage cannot be read as a result — which is this issue's whole subject.

### The discriminators, run against the fakes rather than reasoned about

* A nearest-neighbour **stretch** of the same frame reads **53.91%** in the centre against the real
  extension's 2.17%: a factor of twenty-five.
* **0 of 85** margin columns are identical to the panel's first. A smear would be 85 of 85.
* The centre's 2.17% residual has **0 of 2671** pixels within 8 columns of either crop edge. It is
  interior, which is what a screen-space dither phase shift looks like when every pixel moves 86
  columns, and not the boundary artefact a mis-centred crop would produce.

### What it still does not say

That the extra geometry is CORRECT. Nothing available can say that without a 16:9 reference, and
none exists. It says the coverage is present, varied, and not fabricated from the 4:3 frame — which
is exactly the difference between widescreen and stretching, and no other instrument here measured
it.
