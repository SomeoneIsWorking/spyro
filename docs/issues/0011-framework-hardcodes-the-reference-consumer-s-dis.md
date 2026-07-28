---
id: 11
title: Framework hardcodes the reference consumer's disc env var (5th instance of this defect class)
status: open
symptom: Every disc_read_sector returned failure and the loader moved 0 bytes, presenting as 'the game loads nothing' rather than as a configuration error.
tags: framework,cd
created: 2026-07-28
updated: 2026-07-28
---

disc.c's resolve_disc_path() consults PSXPORT_TOMBA2_DISC, then generic PSXPORT_DISC, then .env, then a drop-in *.chd. A second consumer setting its own PSXPORT_SPYRO_DISC matches none of them, so disc_open() fails and every sector read returns 0 SILENTLY (the caller sees a failed read, not a misconfiguration).\n\nWorked around game-side by bridging PSXPORT_SPYRO_DISC -> PSXPORT_DISC in main.cpp.\n\nThis is the FIFTH instance of the same defect class found by standing up a second consumer: a reference-consumer-specific value baked into the agnostic framework (after the recompiler seed set, the plat-hle sync addresses, the unguarded renderFadeState hook, and the per-overlay RecompRegistry members). Worth an upstream fix — the game should supply its disc-path source, e.g. via GameConfig or an explicit setter — rather than each new port discovering this the hard way.
