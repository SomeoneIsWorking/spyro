---
id: 79
title: Executable argument always installed Spyro 1 runtime before identity inspection
status: resolved
symptom: Passing a Spyro 2 or Spyro 3 executable to spyro_port would construct Spyro1Runtime and install the SCUS_942.28 substrate without verifying the serial bytes
tags: runtime,identity,serial,spyro2,spyro3
created: 2026-08-22
updated: 2026-08-22
---

Root cause: game/core/main.cpp installed a process-global Spyro1Runtime and generated substrate before even reading argv, so the executable path could not influence runtime ownership. Resolution: the shipping selector now identifies the basename serial and validates exact size, PS-X header, and SHA-256 before selecting a derived runtime. The selected runtime owns substrate installation; Spyro 2/3 explicitly refuse before Game because no generated substrate exists. The both-answer C++ test covers exact, unsupported, mutated, and renamed inputs. Real SCUS_942.28 selects and runs Spyro 1; real SCUS_944.67 selects Spyro 3 and stops at the honest no-substrate boundary.
