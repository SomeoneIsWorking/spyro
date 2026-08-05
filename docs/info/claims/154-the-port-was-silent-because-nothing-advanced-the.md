---
id: C154
kind: claim
status: holds
created: 2026-08-05
tags: audio,spu,vsync,frame-boundary,psxport
depends: game/core/vsync.cpp#vblank_wait, game/core/main.cpp, external/psxport/runtime/recomp/spu_audio.cpp
---

## Claim

The port was SILENT — every SPU voice, not just CD audio — because nothing ever advanced the SPU mixer: `main.cpp:72` opened the audio sink with `spu_audio.init()` and `spu_audio.frame()` was called NOWHERE in the repo. Driving it once per displayed frame from `vblank_wait` (the port's real per-frame boundary) produces real game audio.

## Evidence

STATIC, with its denominator: `grep -rn 'spu_audio' game/` returned exactly two lines before the fix — the declaration in main.cpp:19 and `spu_audio.init()` at main.cpp:72 — and ZERO `spu_audio.frame()` call sites. The framework advances the mixer in `native_step_frame` ("tick + per-vblank audio + present + pace"), and that loop NEVER RUNS in this port because the guest still owns its frame loop (game_hooks.cpp), so the per-vblank half was simply absent. Same omission and same cause as spider1's silent intro, found there first. MEASURED, output on the real disc: a 90 s headless run with PSXPORT_WAV captured scratch/wav/spyro_boot.wav, and the mixer filled the capture's 600 s cap (headless is unpaced, so the mixer runs ~6.6x realtime — this is NOT a claim about pacing). Sampled across the capture, left channel, 1 s windows: t=3 s is a single 8-sample-period voice (5 distinct values, AC-RMS 432); t=30 s 9097 distinct / AC-RMS 2101 / range -6034..9443; t=180 s 11995 / 2941 / -9346..11143; t=480 s 16280 / 4792 / -22106..21275; t=590 s 12545 / 3126. The analyser separates both classes — it reported the near-degenerate 5-value tone at t=3 s and the rich signal later from the same file, so "audible" is not an artefact of the measurement. REQUIRED alongside it: the psxport pin moved de187614 -> fd0f59f5, because this port binds `audioMixFrame` to nullptr (game_hooks.cpp:101) and the older framework called that OPTIONAL hook unguarded — verified by running it first, which segfaulted at spu_audio.cpp:176 with the backtrace bottoming out at address 0.

## Narrowing (2026-08-05, same session)

The unexplained DC offset is NOT from the CD-audio path this change wired up. MEASURED: `PSXPORT_DEBUG=xa` over a 45 s boot shows the XA streamer never activates — every ReadS logs "ReadS but XA bit not set (mode=A0) - ignoring", which is correct (mode 0xA0 has bit 0x40 clear, so these are DATA reads, not XA). With no stream active `CDC_GetCDAudioSample` returns silence by construction, so the offset is produced entirely by the SPU VOICE mix. The character of it at t=3 s — an 8-sample period (5512.5 Hz) waveform with only 5 distinct values, entirely positive at 863..2087 — is one voice looping a very short ADPCM block, present from the first bucket of the capture.

Whether that is the game's own boot sound or a decode fault is NOT decided here, and a histogram cannot decide it: it needs the oracle, i.e. the framework's SBS differential harness comparing this SPU against Beetle's on the same input. Recorded rather than guessed.

## What would falsify it

if a windowed run is still silent to the user, then advancing the mixer is necessary but not sufficient and the remaining fault is in the SDL sink; equally, if the rich signal measured above turns out to be reverb or an uninitialised-voice artefact rather than the game's own music, the "real game audio" reading is wrong — that distinction was NOT made here and needs a listen. Scope: this establishes audio is PRODUCED and reaches a WAV capture headless. It does NOT establish correct MIXING, correct pitch, or pacing: the mixer ran ~6.6x realtime in this unpaced headless run, and a small DC offset (~250-400) persists across the capture and is unexplained. Not confirmed by the user in a window
