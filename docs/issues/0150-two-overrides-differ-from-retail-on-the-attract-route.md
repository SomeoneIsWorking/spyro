---
id: 150
title: Two native overrides differed from retail because a nested guest call ran with the override's own `$ra`
status: resolved
symptom: `tools/reach_corpus.py`'s attract-demo route reported one mismatching sampled call each in `camera_collision_update` (0x80034480) and `allocate_particle_slot` (0x80053570), identically at `7d7f2eb` and at `a915b23`
root_cause: `psx::cpu::dispatchGuest` runs a nested guest call with `core.r[31]` as the callee's return address, and an override calling `psx::cpu::callGuestNow` left that register holding the OVERRIDE's caller address — a value the retail body never leaves there. `allocate_particle_slot` exits with `$ra` directly; `func_8004BE4C`, called by `camera_collision_update`, spills `$ra` into a global save area that outlives the call.
fix: `spyro::callGuestJumpedFrom` (game/core/native_execution.h) sets `$ra` to the address the guest's own `jal` at the named site leaves, and every nested call in the two overrides names its `jal`. `tools/override_call_sites.py` re-derives all 29 of those sites and their callees from the provisioned executable.
tags: overrides,differential,attract-demo,camera,particles,call-abi
created: 2026-09-30
updated: 2026-10-01
---

## The measurement that opened it

`tools/reach_corpus.py`'s attract-demo route reported one mismatching sampled call each in
`camera_collision_update` and `allocate_particle_slot`, identically at `7d7f2eb` and at `a915b23`:

```
camera_collision_update @0x80034480 call 3328 (sample 68): first difference
  ram [0x00077E04,0x00077E06): original 0C46 native 1473
allocate_particle_slot @0x80053570 call 39680 (sample 636): first difference
  register ra: original 0x800535A0 native 0x8008A4BC
```

It was **not** a regression from moving the field delivery into `game/core/field_owner.*`: the two
builds' guest RAM is byte-identical at present 10,000, their field traces are identical over 35,734
delivered fields, and their logs differ only in two log strings. See
[0149](0149-shared-field-owner-measured-against-spyro-1-s-own.md).

## EVERY call, before the fix

One sampled call is a depth measurement, not a rate: the route ends when its 420-second clock
expires, so a slower host samples fewer calls and a run that ends early reports zero. So both
overrides were shadowed on **every** call of the attract route
(`PSXPORT_OVERRIDE_DIFF=camera_collision_update,allocate_particle_slot`,
`PSXPORT_OVERRIDE_DIFF_EVERY=1`, 60,568 delivered fields, 0 refusals):

| override | calls seen | sampled | match | mismatch | incomparable | first mismatch |
|---|---|---|---|---|---|---|
| `allocate_particle_slot` @0x80053570 | 44,000 | 44,000 | 43,960 | **40** | 0 | call **17,671**, `register ra`, original `0x800535A0` native `0x8008A5C0` |
| `camera_collision_update` @0x80034480 | 6,894 | 6,894 | 6,863 | **28** | 3 (the original path serviced an interrupt) | call **3,324**, `ram [0x00077E04,0x00077E06)`, original `0C46` native `1473`, 0 registers differing |

Evidence: `scratch/every1/every1.diff.json` and `scratch/every1/every1.log`.

## The cause, from the retail bytes

`psx::cpu::dispatchGuest` runs a nested guest call with whatever is in `core.r[31]`
(`runtime/cpu/native_dispatch.cpp:360`):

```cpp
return core.lightrecExecutor().executeFunction(guestAddress, core.r[31], budget);
```

An override calling `psx::cpu::callGuestNow` therefore ran every callee with the OVERRIDE's caller
address in `$ra`, because nothing in the override ever set it. Retail's `jal` sets it to the
instruction after its delay slot. Both mismatches are that one value, in the two places it is
observable.

### `allocate_particle_slot`, 0x80053570

`external/spyro-1/asm/particle_alloc.s`, the full-pool arm:

```
0x80053594  addiu $a3, $ra, 0x0     ; the caller returns through $a3, not through $ra
0x80053598  jal   rand              ; $ra <- 0x800535A0
0x8005359C  addiu $a2, $a0, 0x0     ; delay slot
0x800535B8  jr    $a3               ; delay slot: sb $a2, 0x1($v0)
```

`$ra` is clobbered by that `jal` and never restored, so retail exits with `$ra = 0x800535A0`. The
override's `callGuestNow` left `$ra` at the dispatcher's continuation instead. Nothing in the guest
can observe it — the body returns through `$a3`, which the dispatcher captured before the override
ran — but the register file is part of the compared machine state, and 40 of 44,000 calls take this
arm (the pool being full), so the sampled run saw one.

### `camera_collision_update`, 0x80034480

The camera body calls `func_8004BE4C` at five `jal`s (0x80034604, 0x800347F4, 0x800348FC,
0x800349EC, 0x80034AD0). That callee does not keep `$ra` on the stack: it spills the whole register
file to a GLOBAL save area on entry, `external/spyro-1/asm/collision.s`:

```
0x8004BE4C  lui  $at, %hi(D_80077DD8)
0x8004BE80  sw   $ra, 0x2C($at)      ; $ra lands at 0x80077DD8 + 0x2C = 0x80077E04
```

`D_80077DD8` is main RAM, so the spilled `$ra` outlives the call and is compared. Retail stores
0x8003460C (the return address of the `jal` at 0x80034604); the native stores the dispatcher's
0x80087314. The judge's first difference is therefore the low halfword of that one word, printed
bytewise as `0C46` against `1473` — two bytes, one register, and 0 registers differing at exit,
because the camera body restores `$ra` from `0x64($sp)` in its epilogue.

**THE SHARED RULE.** A nested guest call runs with the `$ra` the guest's own `jal` left, and a callee
may keep it — in a register, on a stack frame that dies with the call, or in a global that does not.
One helper, one rule:

```cpp
// game/core/native_execution.h
core.r[31] = jalSite + kJalReturnOffset;   // +8: the instruction after the delay slot
psx::cpu::callGuestNow(core, owner, address, args...);
```

The dispatcher captured its continuation before the override body ran, so writing `$ra` mid-body
cannot move the return — `NativeExecutionScope::continuation_` is read at scope entry
(`runtime/cpu/native_dispatch.cpp:105`).

## What was changed

| file | change |
|---|---|
| `game/core/native_execution.h` | `callGuestJumpedFrom` + `kJalReturnOffset`, with the contract and both measurements |
| `titles/spyro1/core/native_particle_alloc.cpp` | the `rand` call names its `jal` at 0x80053598 |
| `titles/spyro1/core/native_camera.cpp` | all 28 nested calls name their `jal`; `testSphericalRow` takes a per-pass `RowCallSites` table, because the three passes are three copies of the row body at three addresses; `SpilledRegisters` now restores `$ra` as well |
| `tools/override_call_sites.py` | the gate that re-derives every such site and its callee from the executable, with a 13-case selftest |
| `tools/override_constants.py` | a `jal`'s own address is now an accepted constant, next to its target and its return address |
| `CMakeLists.txt` | `override_call_sites_selftest` |

29 call sites, all verified: `native_camera.cpp` 28 over 3 overrides with 38 `jal` decoded,
`native_particle_alloc.cpp` 1 over 2 overrides, 0 wrong, 24 modules scanned.

**THE FIRST FIX WAS NOT ENOUGH, and the instrument said so.** With only the call sites changed, the
camera body exited with `$ra` at its LAST `jal` address instead of the caller's: retail restores
`$ra` from `0x64($sp)` in its epilogue, and nothing in the override did. The next every-call run
caught it at **call 2** — `register ra: original 0x80037314 native 0x80034774` — in the first two
minutes of a route that had otherwise reported nothing (`scratch/every1/after/`). The allocator needs
no such restore: its retail body returns through `$a3` on the `jal` arm and through the untouched
`$ra` on every other, which is why its `jal` address is the right exit value there and the caller's
is the right one here.

## The gate

`tools/override_call_sites.py` reads the provisioned executable, decodes every `jal` in every
overridden function the module registers, and requires each `callGuestJumpedFrom` site's address to
be one of those `jal`s **and** the `jal`'s target to be the callee the call passes. Its negative set
is the set of ways to get that wrong: a `jal`'s return address where the `jal` belongs, the callee
of a different `jal`, a site from another function, one wrong row in a three-pass table, a table
whose declared row count disagrees with its initializer, a field no table defines and a site naming
nothing at all. An address in a comment is prose. A missing executable is a refusal (exit 2), never
a pass.

## Evidence after the fix

Attract route, **every** call shadowed (`PSXPORT_OVERRIDE_DIFF_EVERY=1`, 1,200 s, 0 refusals,
`scratch/every1/after2/every1.diff.json`):

| override | calls seen | match | mismatch | incomparable |
|---|---|---|---|---|
| `allocate_particle_slot` @0x80053570 | 22,004 | 22,004 | **0** | 0 |
| `camera_collision_update` @0x80034480 | 3,448 | 3,446 | **0** | 2 (the original path serviced an interrupt) |

For scale: the same route at the same shadowing rate mismatched 40 of 44,000 allocator calls and 28
of 6,894 camera calls before the change, and the first mismatch of each fell at sampled call 17,671
and call 3,324 — both inside this run's denominators.

* `tools/reach_corpus.py` over all six routes, re-run 2026-10-01 on `f92ae0f` plus this change:
  **0 mismatches across all 85 owned overrides**; `allocate_particle_slot` 320 sampled, 320 match,
  `camera_collision_update` 130 sampled, 129 match, 1 incomparable (the original path serviced an
  interrupt). Sampled, not every-call: the every-call run above is the depth measurement.
* `tools/verify.py --jobs 6`: 127 registered tests.

## Still open, and named

Nine other override modules call `psx::cpu::callGuestNow` directly and therefore have the same
defect wherever one of their callees keeps `$ra`: `native_draw_setup` (11 sites), `native_effect_state`
(7), `native_gamepad` (2), `native_moby_helpers` (6), `native_moby_lists` (1), `native_pause_menu`
(20), `native_player_animation` (1), `native_player_physics` (20), `native_random_range` (1). They
are not migrated here — each site's `jal` has to be derived per overridden body and re-gated, which
is its own measurement. The gate reports them as `0 nested call site(s)`, which is a visible count
rather than a silent pass, and migrating one turns that count non-zero.
