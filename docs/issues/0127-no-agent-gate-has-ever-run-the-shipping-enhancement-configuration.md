---
id: 127
title: No agent gate has ever run the shipping enhancement configuration, so 60fps and widescreen are ungated
status: open
symptom: every agent run points PSXPORT_SETTINGS at scratch/spyro-runtime/settings.ini, which does not exist, so the product runs 4:3 at 30fps with every enhancement off. The user plays with psxport_settings.ini (aspect=3, fps60=1). A green agent run says nothing about the configuration the goal is about
state_items: S011
tags: gate,instrument,settings,fps60,widescreen
created: 2026-09-19
---

## Measured

`tools/drive.py` defaults to `--settings scratch/spyro-runtime/settings.ini`. That path does not
exist, so every knob falls to its default. The run says so, honestly:

```
[cfg]   PSXPORT_SETTINGS = scratch/spyro-runtime/settings.ini [env]
[cfg]   PSXPORT_FPS60 = false [default]
[wide] native picture: aspect=0 wide_engine=0 native_width=320 render_width=320
```

The user's own `psxport_settings.ini`, at the repo root, committed to nothing and written by the F1
overlay:

```
aspect=3
fps60=1
```

Pointed at that file the same driver reports the other configuration entirely:

```
[fps60] TRUE per-object interpolated 60fps ON (source: value)
[wide] native picture: aspect=3 wide_engine=1 native_width=512 render_width=512
```

## Why this is a gate defect and not a preference

The project goal is interpolated 60fps and widescreen. Those are the two things every agent run has
had switched off. So the entire body of green Spyro evidence — oracle comparisons, drive routes,
picture checkpoints, the 477-frame Artisans match — was collected against 4:3/30fps, and none of it
is evidence about the product the user actually launches.

The logs are not at fault: they state the configuration every time. Nothing read them. A gate that
runs a configuration nobody ships, and reports PASS, is the failure mode here.

## Not yet established

Whether any of the existing green results CHANGE under the shipping configuration. One run under
the user's settings reached GS_Playing normally, so this is not "everything is broken" — it is
"nothing has been checked there".

## Next

1. Make the shipping enhancement configuration a gated one: either run the agent gate twice (default
   and shipping) or make the shipping configuration the default for gates, and state the
   configuration in the gate's verdict line rather than only in the log body.
2. Re-run the oracle and picture comparisons under `aspect=3 fps60=1` and record what differs.
3. A run whose settings file does not exist should say so by name. Falling back to all-defaults
   silently is how this went unnoticed.
