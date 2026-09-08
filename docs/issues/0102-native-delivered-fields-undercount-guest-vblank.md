---
id: 102
title: Native delivered fields undercount guest VBlank root ticks
status: investigating
symptom: Left 60 native fields moves farther than oracle; counter 0x800749E0 advances 79 over 60 fields and 910 over 610 boot fields
tags: timing,vblank,irq,oracle
state_items: S004,S007,S011
created: 2026-09-08
updated: 2026-09-08
---

Affected state items: [S004](../project-state.md), [S007](../project-state.md), [S011](../project-state.md).

## Observed discriminator

The operator's full-console oracle and native product start at matching Spyro spawn coordinates,
but holding Left for 60 native delivered fields moves farther than 60 oracle fields. Native
REPL counter 0x800749E0 advances 3968 -> 4047 (79) during those 60 delivered fields.
A separate 610-field boot observation advances 1 -> 911 (910). These observations establish
that delivered-field counts and guest root ticks are not equivalent; movement equivalence
must not be inferred from equal requested counts. Runtime attribution of each extra store
is still pending.

## Static ownership conflict

The retained counter is incremented by guest root 0x8005E560 at store 0x8005E58C,
before iterating all eight callbacks at 0x800749C0
(`external/spyro-1/asm/psyq.s:3340`). Its other named store initializes zero at
0x8005E52C. The host fallback increment occurs only when no root ran.

Two shipping routes can invoke that root:

1. `titles/spyro1/core/spyro1_field_scheduler.cpp:100` invokes the root directly, or
   consumes an already-pending enabled VBlank through Hle::irqPoll. Its before/after
   assertion covers only this invocation. `deliver():283` then counts one field.
2. Presentation later calls `FramePresentationBackend::pace`
   (`../psxport/runtime/psx/frame_presenter.cpp:87`), reaching
   `gpu_pace_subframe_fields` (`../psxport/runtime/psx/frame_pacer.cpp:62`).
   This advances display time even with NOPACE, and
   `Timing::advanceDisplayFields` / `raiseVBlank`
   (`../psxport/runtime/psx/timing.cpp:45,103`) latches I_STAT bit 0 and PW_IRQ.
   Lightrec pending-work exits call `servicePendingWork`
   (`../psxport/runtime/cpu/lightrec_executor.cpp:548,568`;
   `../psxport/runtime/cpu/execution_services.cpp:55`). Hle can then resume
   HookEntryInt 0x8005DFC8 -> master dispatcher 0x8005E03C -> the same root,
   outside the scheduler's counted invocation.

Master dispatcher acknowledges the hardware bit before jalr at 0x8005E11C;
the root's return address on that route is 0x8005E124. CPU instruction tick
accounting services SIO/CDC but does not itself raise VBlank. The hardware
VBlank latch is not a count, so two fields advanced together can coalesce into
one extra callback; no fixed extra-tick ratio is assumed.

History commit `380b3e31222568e15d42dfb84ab61b2936d8f657` preserves the
already-pending edge continuation contract. It does not cover an edge serviced
before the next scheduler invocation. Claim C117 also requires the complete
root/table dispatch, including later non-pad callbacks.

## Next falsifier and regression boundary

Catch the guest counter store 0x8005E58C or exact synchronized root entry and
record root RA plus whether FieldScheduler::dispatchCallbacks is on the host
stack. RA 0x8005E124 with no scheduler dispatch frame demonstrates an uncounted
hardware-root invocation. Sample the counter immediately after callbacks,
after presentation, and at the next callback entry to locate its interval.
If extra stores have another origin, revise this candidate rather than masking
the counter or assuming the ratio proves it.

The regression should use the production FieldScheduler, presenter/pacer timing
owner, pending-work service and synthetic registered guest interrupt callbacks:
one delivered field plus presentation plus pending-work service must invoke the
root once; a second field must invoke it a second time. Check emulated display
time and scheduler cadence, plus an unpresented field and temporal split presents.
Preserve the existing shared tests for hardware IRQ delivery, masked-latched
edges, and rational half-field accumulation. A fake pace callback that merely
counts calls cannot expose this failure.

A fix must establish one display-clock owner: native field delivery advances
simulated display time/devices/IRQ once, while presentation only schedules its
host-time share of those delivered fields, or an explicit existing clock boundary
is consumed exactly once. Do not add a title-specific skip-edge boolean, remove
VBlank globally, mask unrelated interrupts, or change counter/timestep constants.
The current pacer contract intentionally couples guest time and IRQ delivery for
other consumers; `test_vblank_irq`, `test_cdc_emulated_time`,
`test_hsync_counter` and `test_pace_plan` constrain that behavior.
