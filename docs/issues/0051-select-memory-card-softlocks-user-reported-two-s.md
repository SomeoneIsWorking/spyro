---
id: 51
title: SELECT MEMORY CARD softlocks (USER-REPORTED): two separate blocking spins, one fixed game-side, one a psxport BIOS-contract defect
status: resolved
symptom: USER: spyro softlocks at the memory card screen. Headless repro: reach 'SELECT MEMORY CARD', press X twice -> no frame is ever presented again, watchdog trips with the guest inside a bare spin loop
tags: memcard,softlock,user-reported,blocker,irq,framework,bios
created: 2026-08-06
updated: 2026-08-06
---

## The user's bug, reproduced headless and deterministically

`replays/bugs/flicker-session.pad` is the USER's own session: 996 frames, START pressed 13 times.
Replaying it reaches the **SELECT MEMORY CARD** screen (Slot 1 / Slot 2) and stays there — present
2200 and present 45000 are the same screen, 12.5 minutes of guest time apart. That screen is ALIVE,
not frozen: 14 present captures spread over the run are all distinct md5s at ~3200 distinct colours,
Spyro's idle animation moving. **This is not the softlock** — a menu waiting for a button press is a
menu waiting for a button press (C071's lesson).

The softlock is what happens when you ANSWER it. Repro, headless, deterministic
(`scratch/mc/pads/two_x.pad` = the user's pad + X at frame 1400 + X at frame 1500):

    PSXPORT_NOWINDOW=1 PSXPORT_NOAUDIO=1 PSXPORT_NOPACE=1 PSXPORT_WATCHDOG=25 \
      PSXPORT_PAD_REPLAY=scratch/mc/pads/two_x.pad ./scratch/bin/spyro_port scratch/bin/SCUS_942.28

The port stops presenting entirely and the watchdog kills it. There are TWO defects behind it, in
series, and they are different bugs.

## DEFECT 1 — FIXED (game/core/vsync.cpp) — MemCardSync's blocking spin

`MemCardSync(mode, cmd, result)` at 0x80067628 (stock Sony libmcrd). `mode == 0` BLOCKS in a bare spin
on the op-complete flag [0x80075B58] — `generated/shard_4.c` gen_func_80067628 label L_80067684, which
is `v0 = [0x80075B58]; beq v0, zero, back`. Nothing else in the loop.

The SOLE writer of that flag is 0x80067CD4, the card driver's completion callback. MEASURED, not
inferred: `PSXPORT_WWATCH=0x80075B58,0x80075B5C PSXPORT_WWATCH_BT=1` over a run to the card screen
logs 280 stores of 1, every one `pc=80069030 ra=80067CFC` (inside 0x80067CD4) with s1=7, and none
from anywhere else. 0x80067CD4 is libetc VBlank callback SLOT 7 (C118) — MemCardOpenSession installs
it with FUN_8005de8c(7, ...). On the console it only ever runs as an interrupt.

This port raises no interrupts; the one place it delivers a vblank is `vsync.cpp`'s override of the
guest's vblank-wait helper 0x8005DD0C, reached only when the GUEST calls VSync. The spin calls
nothing. So the flag can never be set and the guest never returns to its frame loop.

  * unfixed build, same pad, headless: `[watchdog] STUCK: no frame presented` with
    `gen_func_80067628 <- ov_ov_5b800_gen_8007ABAC <- gen_func_8003385C <- gen_func_80012204`.
  * fixed build, same pad, headless: `[card] MemCardSync(0): op 3 ... completed after 2 field(s)`
    and the run proceeds into the card FILE ops (open/lseek/read/write). Defect 2 then bites.

FIX: `memcard_sync` in `game/core/vsync.cpp` — when the guest enters the blocking arm with an op
pending, the port delivers vblanks (by calling `vblank_wait` itself, so there is ONE definition of
"a vblank happened") until the game's own callback completes the op, then super-calls the recompiled
body, which takes its non-spinning path. Nothing is reimplemented and no completion is fabricated:
if the bound (60 fields) expires the body runs anyway and spins, with a `lucent::warn` saying so.
The override is INERT without input — a 120 s no-input headless run logged 0 blocking waits and no
watchdog trip, and `tools/gate.sh 90` is 16/16 PASS (42218 frames, 0 misses, 0 divergences).

## DEFECT 2 — NOT FIXED — psxport's BIOS file-API return contract (FRAMEWORK)

Spyro's libmcrd card READ and WRITE op state machines both retry the BIOS call until it returns ZERO:

    read  op FUN_80066F34 case 0x14 (generated/shard_0.c L_80067018):
        do { v0 = read (fd, buf, len); } while (v0 != 0);
    write op FUN_800671F0 case 0x14 (generated/shard_2.c:8502-8509):
        do { v0 = write(fd, buf, len); } while (v0 != 0);

The stubs resolve to `B0:0x34 read` (gen_func_800684D4 -> r9=52, r10=0xB0) and `B0:0x35 write`
(gen_func_80062FC4 -> r9=53, r10=0xB0). psxport's `runtime/recomp/memcard.cpp` `file_read` (line 324)
and `file_write` (line 351) both set `c->r[V0] = len`, which is non-zero — so both loops never
terminate. And these state machines are stepped FROM the vblank callback, so the spin also stops the
whole frame loop.

Both observed with gdb on the live hung process:
  * card holding a `BASCUS-94228SPYRO` save -> `#2 file_read (memcard.cpp:321)` under
    `Hle::dispatchBios(table='B', fn=52)`.
  * blank card -> `#3 gen_func_800671F0 at generated/shard_2.c:8508` (the `write` call).

WHY 0 IS THE RIGHT ANSWER: the state right AFTER each loop waits on the memory-card completion EVENT
(GetMemCardEventStatus / WaitMemCardEvent), so the BIOS call only STARTS the transfer — 0 means
"accepted", non-zero means "busy, retry". And the game demonstrably works on real hardware, so the
real BIOS cannot be returning `len` here. Errors should be reported by delivering the SwCARD/HwCARD
ERROR spec (0x8000) instead of the I/O-end spec (0x0004) — the guest's next state maps that to a
retry (up to 16) and then to an access code. `deliverComplete` is already called by both handlers, so
the event half is already right; only the return value is wrong.

BLAST RADIUS, NOT MEASURED HERE: Tomba!2 uses the same BIOS file API (its journal.md names the same
five stubs) but its own notes say its card path runs through the B0:0x4E/0x4F FRAME primitives.
Spider-Man's codemap calls its card check done. Whoever lands this must re-gate both — I did not.

## What is NOT covered
* Whether the menu behaves correctly once defect 2 is fixed (slot select -> load/new game) is
  unmeasured; the port has never been past this screen.
* `WaitMemCardEventSlot1` 0x80068264 is a second bare spin (on the same flags). It is only ever
  entered after GetMemCardEventStatus returns non-zero, so it exits immediately today — but it is
  the same shape and would deadlock the same way if that ever stopped holding.

### Note (2026-08-06)
CONTROLLED A/B — the negative control, run after this entry was filed. ONE tree, ONE line toggled (the `shard_set_override` for MemCardSync present vs replaced by a `(void)` cast), rebuilt per leg, same pad `scratch/mc/pads/two_x.pad`, same headless env, each leg given its OWN BLANK card image so both start from identical card state.

| leg | outcome |
|---|---|
| control | `[watchdog] STUCK: no frame presented`, backtrace `gen_func_80067628 <- ov_ov_5b800_gen_8007ABAC <- gen_func_8003385C <- gen_func_80012204` (scratch/mc/logs/abB_control_fresh.log) |
| fix | `[card] MemCardSync(0): op 1 (arg 0x0) completed after 2 field(s), result 0` — the override installed AND ran — and the wedge MOVES to `gen_func_800671F0 <- gen_func_80068FC4 <- gen_func_80067CD4`, defect 2's write loop (scratch/mc/logs/abB_fixed_fresh.log) |

MEASURED CAVEAT: on a card that ALREADY holds a `BASCUS-94228SPYRO` save, even the CONTROL leg gets past MemCardSync and wedges in `card_hle_b0` (the READ loop). So defect 2 is reachable independently of defect 1, and fixing defect 1 alone does NOT get the port past the screen in either card state.

THE STEP GATE IS NOT MET. The last present before the wedge is still the SELECT MEMORY CARD screen (scratch/mc/shots2/present_1350.ppm, 3119 distinct colours, checked against its own present_shot log line). The port has never been past this screen.


---

# RESOLVED 2026-08-06 — and BOTH halves above named the wrong code

The port now gets past SELECT MEMORY CARD, creates a save file on a blank card and reaches the game's
save-slot menu. Three things were actually wrong; two of them are not what this entry originally said.

## 1. The port never ARMED the deferred-work gate (game-side, one line)

The entry above says "This port raises no interrupts. The spin calls nothing." The second sentence is
right. The first is **false and was the reason a per-call-site override got written**:
`tools/recomp/emit.py:1257` emits `if (c->pending_work) rec_irq_poll(c);` on every loop BACK-EDGE, and
it is present in this very spin (`generated/shard_4.c` L_80067684 carries the gate, one line above the
load). `rec_irq_poll` services `PW_HOST` via `rec_host_turn`. Nothing in this repo ever called
`rec_host_turn_register`, so the gate was never armed — spider1 does it in one line
(`game/core/sync_native.cpp:549`).

FIX: the title-owned `FieldScheduler` registers a host turn after native boot and uses its one
`deliver` operation for asynchronous fields, so there is one definition of "a display field
happened". No override on 0x80067628, no bound, no `kMaxVblanksPerCardWait`. The rejected diff
(`coord/patches-gameside/spyro/REJECTED-memcard-sync-override.diff`) is not needed and is not applied.

## 2. Running the guest's vblank handler at an arbitrary point CRASHES this game (C163)

This is the part nobody had hit, because nothing had ever delivered a field asynchronously here.
`$sp` is not a stack pointer at an arbitrary PSX instruction — the kernel's exception entry switches
stacks, so a hand-written renderer may keep scratch in `sp`/`gp`/`fp`, and Spyro's does:
`gen_func_800258F0` (RenderWorldChunks) writes `sp = -1`, `sp = 0x1F800000` and `sp = r1 + 7680` in its
inner loops. With the handler run on `c->r[29]` the port died on an UNMAPPED read8 at 0x9006E9AB about
half a second in, every run, with and without input.

FIX: `run_vblank_callback` switches to a dedicated handler stack — [0x8000C000,0x8000E000), inside the
BIOS-reserved kernel region where psxport already keeps its own work area (`hle.cpp` HLE_WORK_BASE /
HLE_B0TABLE / HLE_C0TABLE). Which part of that region is free was MEASURED, not assumed:
`PSXPORT_WWATCH=0x80000000,0x8000FFFC` over a whole run to this screen writes exactly 131 distinct
addresses in that 64 KB (0x80000000-0x8000007F from a B0 stub at 0x80068900, plus psxport's own
0x8000F16C / 0x8000F800-07) and nothing else. The stack is poison-filled and its low-water mark is
reported: measured peak **120 bytes of 8192**, and the floor word is checked after every dispatch.

## 3. DEFECT 2 was in `hle.cpp`, not `memcard.cpp` — card WRITES never reached the card (C164)

`memcard.cpp file_write` returning `len` was real but **unreachable**: `Hle::dispatchBios`'s B-table had
its own `case 0x35` above the `default: card_hle_b0(...)` arm which returned `len` for every fd. So every
memory-card write in this framework was silently discarded while reporting success. Measured:
`PSXPORT_DEBUG=card,bios` logs 3,986,996 `B0:0x35(fd=3, buf=0x80185BB0, len=0x1400)` calls and ZERO
`[card]` lines (`scratch/mcfix/logs/fix2.log`). There is no `case 0x34`, which is why READ did reach
`file_read` and the read half of the diagnosis was correct.

FIX (framework, claim area `memcard-bios`): delete that case so B0:0x35 falls through to the card module
(whose `file_write` already implements the fd 1/2 console arm identically — one definition); return **0**
from `file_read`/`file_write` on a card fd (transfer accepted, completion arrives as the card event);
make `readFrame`/`writeFrame` return whether the frame actually moved instead of silently no-opping; and
complete an unperformable transfer with the ERROR spec (0x8000) instead of I/O-end, which is the only
channel a libmcrd consumer can see a card failure through. An invalid fd stays a synchronous -1.

RED-first test: `external/psxport/tests/test_memcard_file_api.cpp`, hermetic, entering through
`Hle::dispatchBios` — the first version called `card_hle_b0` directly and was GREEN against the broken
build, which is exactly the blind spot that hid defect 3 for a whole session.

## 4. Fallout the gate caught: the host clock breaks the per-call differential (C165)

`tools/gate.sh 90` went to FAIL with 2 native/substrate divergences the moment the host turn was armed.
Not a native-body bug: only `ndiff_run`'s SUBSTRATE leg contains the `pending_work` gate, so a host turn
can only ever land in that leg. The differing bytes name it — the vblank counter 0x800749E0, the
pad-decoder counters, the pad buffers and the handler stack. `ndiff_run` is now a critical section that
`rec_host_turn` defers across (PW_HOST left armed, so no field is lost).

## Evidence, and the negative control

| leg | build | outcome |
|---|---|---|
| A (control) | host turn NOT registered, card fix present | presents STOP: `PSXPORT_PRESENT_SHOT_AT=1200,1350,1500,1700,2000,2500,3500` wrote only 1200/1350/1500 — the later shots do not exist because nothing presented. Watchdog `STUCK` with `gen_func_80067628 <- ov_ov_5b800_gen_8007ABAC`. Last picture: "THE MEMORY CARD IN SLOT 1 DOES NOT HAVE A SAVE FILE / CREATE SAVE FILE NOW?" (`scratch/mcfix/logs/shotA_control.log`, `scratch/mcfix/shotsA/`) |
| B (host turn only) | host turn registered, card fix absent | wedge MOVES to `gen_func_800671F0 <- gen_func_80068FC4 <- gen_func_80067CD4` — the card WRITE retry loop, running inside the vblank callback (`scratch/mcfix/logs/ht2.log`) |
| C (both) | shipping | all 7 shots written; present 1700/2000/4000 show the NEXT screen — "EMPTY EMPTY EMPTY / NEW GAME | LOAD GAME / Using Card In Slot 1", and the blank card image now holds a real `BASCUS-94228SPYRO` directory entry (`scratch/mcfix/logs/final.log`, `scratch/mcfix/shotsFinal/`) |

Same pad (`scratch/mc/pads/two_x.pad`), same headless env, a fresh blank card image per leg.

**`tools/gate.sh` CANNOT see this path — it feeds no input at all**, so its 16/16 PASS is a
no-regression statement and nothing more (before: 57511 frames / 0 divergences / 160 bodies verified;
after: 53620 / 0 / 160). The gate that CAN see it is the pad-replay repro in the table above, and its
discriminator is whether present shots after 1500 exist.

## Still not covered
* Whether NEW GAME / LOAD GAME from the slot menu works — the run reaches the menu and idles there.
* A card that already holds a save: only the blank-card path was re-measured after the fix.
* B0:0x4E/0x4F (`card_read`/`card_write`, the frame primitives Tomba!2 uses) still ignore the new
  `readFrame`/`writeFrame` return value and always announce completion. Same class of defect, left
  deliberately: changing it touches a path this run did not gate.
