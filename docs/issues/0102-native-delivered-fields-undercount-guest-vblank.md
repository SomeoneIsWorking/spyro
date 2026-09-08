---
id: 102
title: Native delivered fields undercount guest VBlank root ticks
status: resolved
symptom: Guest VBlank callbacks ran both from native field delivery and presentation-generated hardware edges
tags: timing,vblank,irq,oracle
state_items: S004,S007,S011
created: 2026-09-08
updated: 2026-09-08
---

Affected state items: [S004](../project-state.md), [S007](../project-state.md), [S011](../project-state.md).

## Cause and correction

The retained counter at 0x800749E0 increments in root 0x8005E560, store 0x8005E58C,
before all eight callbacks at 0x800749C0 (`external/spyro-1/asm/psyq.s`).
FieldScheduler invoked that root, but presentation subsequently advanced Timing and
latched another hardware VBlank. Shipping JIT pending-work service could resume
HookEntryInt 0x8005DFC8 -> master 0x8005E03C -> the same root outside the scheduler's
local before/after check. Two-field presentation quotas could coalesce into one
extra hardware edge; there is no fixed duplicate ratio.

FieldScheduler now advances one physical display field before interrupt service.
Spyro1Runtime's presentation override waits for the host deadline without advancing
devices again. Other runtimes retain the framework's combined time-and-wait behavior.
Per-Game FramePacer state replaces a process-global deadline.

An installed HookEntryInt path retains root/counter ownership while masked or in a
critical section. The scheduler leaves its hardware edge pending and does not call
the root directly or synthesize a counter increment. Hle owns the shared CPU-context
readiness condition. Two masked physical fields coalesce into one latched IRQ, as
hardware does; counter ticks need not equal physical fields during such deferral.
Direct no-Hook callbacks and root-absent bootstrap counter ownership remain explicit.

The production scheduler/presenter/pending-work regression failed with counter 2 for
one field before this correction. A second discriminator exposed the same duplicate
on unmask. The corrected fixture passes 9 cases / 997 checks, including the actual
Core presenter, Fps60 endpoint/midpoint splitting, unpresented time, all eight callback
slots, GPR/HI/LO/PC restoration, masked/critical deferral, no-Hook and bootstrap.
Callback cases execute shipping JIT blocks with zero fallback.

## Exposed runtime cost

The first corrected-clock game run hit the unchanged three-second watchdog inside
retained libpad IRQ handling. GDB samples reached the legitimate pipelined final
RX wait at 0x8006A0B0 (RA 0x8006AFAC) and the timer-based ACK wait at 0x8006BBD4;
the latter had a pending ACK only 36 emulated ticks ahead. Transfers were progressing.
Every JIT exit/reentry called gte_bind, which cleared the unused 1 MiB Pgxp cache.
Retained pending IRQ work made those clears recur around small controller-poll blocks.

No shipping precision-cache readers remained in the framework or maintained consumers.
The obsolete cache, storage and lookup/reset shims are removed. Required Beetle ABI
hooks remain stateless; explicit projection provenance and GTE isolation are preserved.
Five focused GTE/provenance tests pass. No watchdog, SIO timing or pending-work policy
was relaxed. The subsequent real game run passes the same title-screen region.

## Real-game comparison

The silent, windowless native/Lightrec product with widescreen and fps60 enabled:

- Boot/title: counter 1 -> 611 over 610 delivered fields; before correction 1 -> 911.
- Artisans Left: counter 3979 -> 4039 over 60 delivered fields; before correction
  the observed 60-field interval advanced 3968 -> 4047.
- Native spawn XYZ: 84992, 47173, 9557. After Left 60: 84356, 46546, 9692.
- Independent console oracle, user's NTSC-U SCPH-1001 v2.2 BIOS: spawn
  84992, 47173, 9556; after Left 60: 84357, 46544, 9691.
- Native camera after Left: 86564, 45526, 10301; oracle: 86624, 45576, 10308.

The moved native picture now keeps Spyro visible above the fountain wall, with
additional horizontal coverage. The 1–2-unit player differences and remaining camera
state differences are not exact-state or complete visual parity. Native REPL inspection can
interrupt guest update or run in the post-present field tail; oracle retro_run stops at a
scanout boundary and can likewise stop mid-update. Neither sample proves matching game
phase. Align completed camera/render checkpoints before attributing the residual difference
to gameplay or rendering. The oracle has an independent CPU/scheduler but shares Beetle
device lineage.

The complete native observation exits 0 at 4,061 fields / 2,062 product steps and
presentation fences: 3,717 translations, 21,893,233 executed JIT blocks,
183,465,800 instructions, zero faults and zero fallback. Paired temporal proof records
1,050 midpoint and 1,050 endpoint callbacks, 2,100/2,100 emitted. This proves the reached
clock and paired-player path, not complete FIELD interpolation or released-host performance.

A recurrence of extra root ticks outside legitimate masked-edge deferral, failure of
any production clock/IRQ regression, or a mismatched device-time advance falsifies this
resolution. Broader camera/visual and paced audio/performance qualification remains S011.

## Landing verification

Framework `e6dd7256` passes all 137 checks. The pinned Spyro Clang/Ninja gate passes
16 CTests, clang-tidy for 115 translation units, and formatting for 204 source/header
files. The no-argument `./run.sh` path builds with Clang, provisions the authenticated
input, and exits 0 after an explicitly bounded one-field startup (zero fallback).
That launcher check is separate from the representative gameplay observation above.
