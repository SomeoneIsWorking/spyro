---
id: 123
title: The port never reports a card as NEW since power-on, so the game skips its own "CREATING SAVE FILE..." page
status: open
symptom: at the save_picker checkpoint the console reference shows "CREATING SAVE FILE..." and the product shows the three-slot picker; the product reaches the checkpoint 61 game frames sooner, and 51.89% of pixels differ. Proven 2026-09-19 NOT to be a card-format difference: giving the reference the product's own formatted card is byte-identical
state_items: S019
tags: memcard,oracle,picture,parity
created: 2026-09-19
---

## The observation

`tools/picture_oracle.py --bios ../SCPH1001.BIN` at `save_picker`, with the state check of issue
0119 live and every picture-decisive range EQUAL:

| | console reference | product |
|---|---|---|
| reached `save_picker` after | 748 game frames | 687 |
| shows | `CREATING SAVE FILE...` on the panel | three `EMPTY` slots, `NEW GAME` / `LOAD GAME` |

51.89% of pixels differ. Both pictures are correctly drawn -- frame chrome, the "Using Card In Slot
1" line, and the world and Spyro behind the panel all match. The panel contents are different pages
of the same dialog.

## The mechanism -- CORRECTED 2026-09-19; the section below it was wrong

**The premise that the two cores get differently FORMATTED cards is false, and it was measured
false.** Both cards are formatted. `vendor/beetle-psx/mednafen/psx/frontio.c`
`InputDevice_Memcard_Ctor` ends with the comment `/* Init memcard as formatted. */` and a call to
`InputDevice_Memcard_Format`, which writes the same `MC` header and 15 free directory entries the
product writes. The reference has never handed Spyro an unformatted card.

The falsifier was run. `tools/oracle/compare.py` grew `--console-card CARD.MCR`, which starts the
reference from a given 128 KiB image through the libretro save-RAM pointer the core exposes for
slot 1. Handed the product's own formatted blank card (`MC`, dir entry 0 = `0xA0` free, sha256
`77d33c6b…`), the run is **byte-identical to the baseline**:

| | baseline | reference given the product's card |
|---|---|---|
| console reaches `save_picker` | 748 game frames | 748 |
| product reaches `save_picker` | 687 | 687 |
| pixels differing | 63768/122880 (51.89%) | 63768/122880 (51.89%) |
| worst tiles | (256,176):255 (352,192):255 | identical |

Making the card images equal changed nothing, so the card's CONTENTS were never the difference.

### What the difference actually is

A PSX memory card answers the device-select flag byte with **bit 3 (0x08) set from power-on until
the console successfully WRITES a frame to it**. That is a property of the card device, not of the
bytes on it: a freshly formatted card still reports new. The BIOS turns that flag into `EvSpNEW`,
and that is what makes Spyro draw `CREATING SAVE FILE...`.

Beetle models exactly this (`frontio.c`): `presence_new` is set in `InputDevice_Memcard_Power`,
transmitted as `self->transmit_buffer = self->presence_new ? 0x08 : 0x00` on device select, and
cleared in ONE place -- the write-end path, after a frame is committed. Not on a read, and not
after the first announcement.

`runtime/psx/memcard.cpp` has no such flag. `card_hle_a0` answers `_card_info` and `_card_load`
with `deliverComplete` + `V0 = 1` unconditionally, which tells every title that every card has
already been used with it. That is why the port skips the first-run page, and it is why the
pre-format looks like the cause: pre-formatting makes an unconditional "all fine" answer true
enough to get past the check.

### The faithful implementation was written and measured, and it is not enough

`Memcard` gained `mNewSincePowerOn` (true at construction, cleared in `writeFrame` on a successful
frame write -- the hardware lifetime exactly), `deliverNewCard` (EvSpNEW on both classes), and
`card_hle_a0` reporting NEW while the flag stands. Built, driven with `tools/drive.py gameplay`:

```
drive.py REFUSED: never reached the save picker within 12000 frames;
  last gamestate=13 title=TitleState(mode=1, state=2, tick=140, sub_tick=6, sub_state=0, option=0)
[card] BIOS card syscalls (at shutdown): 5456 call(s), 0 unhandled.
       called: A0:0xAB=2166, A0:0xAC=1084, B0:0x35=38, B0:0x4E=1084, B0:0x50=1084
```

Byte-identical to the earlier "EvSpNEW announced every call" arm, and for a reason that is now
clear: **no card frame is ever written, so the flag can never clear.** `B0:0x32` open is 0 and the
38 `B0:0x35` writes are not card writes (nothing is open), so `writeFrame` is never reached. The
flag standing forever and "announce on every call" are the same run.

Reverted. Shipping it replaces a product that reaches gameplay with one that stalls, which is worse
for a player than the known divergence. The pre-format remains a STOPGAP, now with the right name
on it: the port cannot report a new card, and it cannot serve the creation path a new card starts.

## The earlier mechanism section, retained because the harness detail in it is still true

The two cores are not given the same card, although the harness intends to.
`external/psxport/tools/oracle/compare.py::fresh_card` points the product at a path that does not
exist "so it formats a blank card the way the console reference starts with one". The product then
does exactly that, and one step more: `external/psxport/runtime/psx/memcard.cpp` creates the missing
file zero-filled (line ~116) and then FORMATS it -- "Standard PSX layout: frame 0 = 'MC' magic;
frames 1..15 = free directory entries. Only formats when UNFORMATTED" (line ~144), logging
`formatted blank card image (MC header + 15 free dir entries)`.

So the product hands Spyro an already-formatted card. The reference hands it an unformatted one, and
Spyro runs its own creation path -- which is what a real PSX with a brand-new card does. The 61
frames the product saves are that page.

## Why this is a product difference and not only a harness one

The comment gives the reason for pre-formatting: "so the file API can allocate directory blocks
immediately... open(create) would find no free block and saving would fail". That is the port
standing in for a step the guest is supposed to perform through the BIOS. The consequence is that
the game's own format/create path never executes in the port, on any card, so nothing exercises it
and a player with a new card sees a different first-run sequence from the original.

## The falsifier was run, and it answered the SECOND branch

Recorded falsifier: suppress the pre-format and drive the same route. If Spyro then shows
`CREATING SAVE FILE...`, the pre-format is the whole cause; if the product instead fails to save,
the port's file API cannot allocate on an unformatted card and that is what the pre-format hides.

Run 2026-09-19 with the pre-format disabled (`if (false && ...)` at memcard.cpp:152, rebuilt,
reverted afterwards):

```
[picture] FAILED: native: save picker not reached within 6000 game frames; last
          Observation(gamestate=13, title=TitleState(mode=1, state=1, tick=5320, sub_tick=5319,
          sub_state=12, option=0), game_tick=0, level=0)
[picture] console: save_picker after 748 game frames
```

**The second branch.** The product does not reach the picker at all -- it sits in title sub_state 12
for 6000 game frames while the reference gets there in 748. The pre-format is not cosmetic; without
it the port cannot proceed past the card check. So this is a STOPGAP standing in for a guest path
the port does not serve, exactly as its own comment says.

## What it is NOT: B0:0x41 format()

The obvious candidate was the BIOS `format("bu00:")` vector, which `card_hle_b0` does not handle.
Implemented it (extracting the layout writer into one `Memcard::formatCard()` owner so the constructor
and the syscall could not drift), removed the pre-format, rebuilt, re-ran: **byte-identical failure**,
same sub_state 12, same 6000 frames. Spyro does not call B0:0x41 here. The change was reverted rather
than left in the tree as an unexercised handler.

## The instrument that was going to answer this did not work, and that was the finding

The step recorded above said `card_overrides_init` enables `Memcard::setVerbose` when the lucent
channel `card` is on, so "every B0 file handler then logs its function and arguments". That was
wrong in two ways, and both were measured on 2026-09-19:

* `mVerbose` gated 14 log sites, but it was latched ONCE at `card_overrides_init` from
  `lucent::channel_on("card")`. With `PSXPORT_DEBUG=card` set, a run that reached gameplay through
  the save menu and a run that stalled in title sub_state 12 for 12,000 fields emitted the SAME two
  card lines. The flag was answering the channel question at the wrong time, for the whole run.
* The dispatcher's `default: return 0` -- "this runtime has no implementation" -- logged and counted
  nothing at all. A missing handler was invisible by construction.

So the silence was the instrument, not the guest, and reading it as "Spyro makes no card calls"
would have been a false conclusion drawn from a broken tool. Fixed in psxport: the 14 gated calls
are now plain `lucent::debug("card", ...)` (the logger owns channel filtering, and it asks per
call), `runtime/psx/card_syscall_log.*` counts every dispatched function handled and unhandled, and
`Game::~Game` reports the totals with their denominator whether or not anything was called.

## What the working instrument says

Same route, same binary, the only difference being whether the card image was pre-formatted:

| function | reaches gameplay | stalls in sub_state 12 |
|---|---|---|
| `A0:0xAB` `_card_info(port)` | 55 | **5135** |
| `A0:0xAC` `_card_load` | 2 | 3 |
| `B0:0x32` open | 5 | 3 |
| `B0:0x33` lseek | 2 | **0** |
| `B0:0x34` read | 1 | **0** |
| `B0:0x35` write | 39 | 38 |
| `B0:0x36` close | 3 | **0** |
| `B0:0x4E` `_card_read` | 2 | 2 |
| `B0:0x50` `_card_chan` | 2 | 2 |
| **unhandled** | **0** | **0** |

Nothing is missing a handler. With an unformatted card Spyro opens, never seeks, never reads, never
closes, and polls `_card_info` 5,135 times.

## The cause

`card_hle_a0` answers `_card_info(port)` -- and `_card_load` -- the same way whatever the card holds:

```
Memcard::deliverComplete(c);
c->r[V0] = 1;
```

It always reports the operation completed successfully. It has no way to say "this card is not
formatted", which is exactly the state a real PSX reports for a brand-new card and exactly what
makes the console reference draw `CREATING SAVE FILE...`. Spyro asks, is told everything is fine,
finds the card unusable, and asks again -- forever. Pre-formatting the image makes the
unconditional answer true, which is why the stopgap works and why the game's own creation path has
never run in this port.

`Memcard::deliverError` already exists for precisely this: its comment says the ERROR event spec is
the ONLY channel a libmcrd consumer can see a card failure through, because the BIOS call's return
value is a "busy, retry" flag and cannot carry one.

## The guest side, recovered from the binary

Spyro opens EIGHT card event specs, all `mode=0x1000` (EvMdINTR), on both classes, and then polls
them with TestEvent. The four handlers are identical five-instruction stubs, each setting its own
flag word -- so unlike Spider-Man's status codes there is no overwrite-ordering hazard here:

| spec | handler | sets |
|---|---|---|
| `0x0004` EvSpIOE | `0x80067DD0` | `[0x80075B2C] = 1` |
| `0x8000` EvSpERROR | `0x80067DE4` | `[0x80075B30] = 1` |
| `0x0100` EvSpTIMOUT | `0x80067DF8` | `[0x80075B34] = 1` |
| `0x2000` EvSpNEW | `0x80067E0C` | `[0x80075B38] = 1` |

The wait routine is `0x80068264`. It spins while all four flags are zero, then returns
`(IOE | ERROR<<1 | TIMOUT<<2 | NEW<<3) >> 1` (`sra $v0, $s0, 1` at `0x80068320`) and clears them:

| what fired | returns |
|---|---|
| IOE | 0 |
| ERROR | 1 |
| TIMOUT | 2 |
| **NEW** | **4** |

Measured with TestEvent traced (61,689 calls over the stalling run): handles `F1000001`/`F1000005`
-- the two IOE specs -- fired 5,139 and 5,140 times. The ERROR, TIMOUT and NEW handles fired **zero**
times each, on both classes. The runtime has only two deliveries in it, `0x0004` and `0x8000`, and
nothing anywhere can emit `0x2000` or `0x0100`.

So the port can only ever make this routine return 0, "completed, nothing special". The
unformatted-card answer is unreachable by construction, which is the mechanism behind the 5,135
re-asks.

## An EvSpNEW attempt, and what it measured

Implemented `Memcard::formatted()` and `deliverNewCard()` (EvSpNEW on both classes), reported it
from `_card_info` when the card lacks the MC magic, and removed the pre-format. Result on a genuinely
blank card:

| | stopgap (pre-format) | EvSpNEW, announced every call | EvSpNEW, announced once |
|---|---|---|---|
| title state | reaches picker | `state=2, sub_state=0, tick=140` | `state=1, sub_state=12, tick=5105` |
| `A0:0xAB` | 55 | 2166 | 5135 |
| `A0:0xAC` `_card_load` | 2 | 1084 | 3 |
| `B0:0x4E` `_card_read` | 2 | 1084 | 2 |
| `B0:0x32` open | 5 | 0 | 3 |
| reaches picker | yes | no | no |

Two findings, one useful and one falsified:

* Announcing EvSpNEW on every call MOVES the guest: it leaves the sub_state-12 loop entirely, enters
  a different title state, and starts cycling load/read/chan 1,084 times without opening a file --
  consistent with a card-scan or creation path, but it does not complete.
* Announcing it ONCE, on the theory that EvSpNEW is a detection rather than a standing condition, is
  byte-identically the original stall (5135/3/3/38/2/2, tick 5105). That theory is dead: the guest
  clears its flags after each read and needs the condition re-asserted.

Reverted, because shipping either arm replaces a stopgap that reaches gameplay with a product that
does not. The pre-format remains, and remains a stopgap.

## What is actually open

The standing-EvSpNEW arm is the closest anything has come: it is the first change that moves Spyro
out of the stall. What it does not do is finish. The 1,084-iteration cycle is now RECOVERED from the binary, and it
is not "reading and rejecting a card image" -- it is Spyro correctly re-handling a standing new-card
condition:

* `0x80068920` is a leaf probe: `_card_chan(chan)` then `_card_read(chan, 0x3F, buf=0)`. Its only
  caller is `0x800666DC`, inside the card state machine.
* The wait routine `0x80068264` returns 4 for NEW, and `0x8006679C` stores that 4 into the card
  condition word `[0x80075B54]`.
* `[0x80075B54]` has exactly two readers outside the state machine. `0x80067D28` is the RESULT
  PUBLISHER: it copies the result `[0x80075B50]` to `[0x80075B94]` and the condition to
  `[0x80075B98]`, clears both, and calls the title's registered callback `[0x80075B90](result,
  condition)`.
* The state handler at `0x800666A4` advances only on condition 0 (-> state `0x1E`) or 3 (re-probe
  -> state `0x15`). A 4 falls through unchanged, so a condition that never stops being 4 re-issues
  forever.

So the guest is being told "new card" truthfully and is doing the right thing with it. What the
port does not serve is the rest of the creation path: the title never gets to open and write a save
file, so the condition never ends. The open question is therefore no longer "what does it reject"
but **which BIOS call in Spyro's creation sequence the port answers in a way that prevents the file
from ever being opened** -- `B0:0x32` open is called 0 times on this path.

Do not re-try the one-shot latch, NEW-on-`_card_info`-only, or "clear the flag on read"; all three
are measured dead, and the third is also wrong against hardware.

Recover the caller of `0x80068264` in that path and see what it does with the returned 4, then make
`_card_read` answer an unformatted card the way hardware does. The discriminator is unchanged: the
blank-card run's `B0:0x33/0x34/0x36` stop being zero and `A0:0xAB` stops running into the thousands.

Do not re-try the one-shot latch; it is measured and dead.

## Bearing on the user's report

The user reported the save menu not matching the oracle. This is that symptom, reproduced and
narrowed: it is a memory-card lifecycle difference, not a rendering defect -- nothing about the
painter, the ordering table, or widescreen is involved -- and the port currently gets past the card
check only because it formats the card behind the guest's back.
