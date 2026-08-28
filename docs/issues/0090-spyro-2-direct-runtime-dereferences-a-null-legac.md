---
id: 90
title: Spyro 2 direct runtime dereferences a null legacy CD config before crt0
status: resolved
symptom: spyro2_port cannot reach crt0 because dc_boot_init calls Cd::overridesInit and Cd::overridesInit dereferences core.cfg even for a direct GameRuntime
tags: spyro2,boot,framework,cd,direct-runtime,blocker
created: 2026-08-27
updated: 2026-08-28
---

Root cause is in the shared framework, not Spyro 2: dc_boot_init unconditionally calls game->cd.overridesInit(). Cd::overridesInit obtains const GameConfig *cfg = game->core.cfg and immediately reads cfg->cdInlineLoad and the remaining legacy CD fields. Spyro2Runtime is correctly direct and binds no Spyro 1 GameConfig, so core.cfg is null by contract. The title-local substrate, executable, runtime, native frame driver, and measured PlatformHlePlan now build, but an actual shipping run would fault before crt0_setup. Proper fix: Cd::overridesInit must accept direct runtimes without a legacy config, registering only title-declared direct CD facts when present; an empty direct CD plan must be a valid no-registration result. Do not add a Spyro 1 compatibility config or duplicate CD HLE semantics in titles/spyro2. After the shared fix, the next isolated run must reach the title-owned boundary immediately before display bootstrap 0x80011BBC, with retail VSync 0x80058EDC still fatal.

### Resolution (2026-08-28)
Resolved in the shared framework: Cd::overridesInit now treats cfg==nullptr as the valid empty legacy-registration group, while typed PlatformHlePlan and legacy GameConfig registrations retain their existing owners. Framework CTest test_cd_direct_runtime_init passes empty-direct, planned-direct, and legacy cases; an isolated Spyro2 real-executable run then passed dc_boot_init, audited crt0 10/10, and reached 0x80011BBC with fatal VSync registration intact.
