---
id: 123
title: The port pre-formats a blank memory card, so the game never runs its own "CREATING SAVE FILE..." page
status: open
symptom: at the save_picker checkpoint the console reference shows "CREATING SAVE FILE..." and the product shows the three-slot picker; the product reaches the checkpoint 61 game frames sooner, and 51.89% of pixels differ
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

## The mechanism

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
out of the stall. What it does not do is finish, and the next question is narrow -- what the
1,084-iteration `_card_load` / `_card_read` / `_card_chan` cycle is looking for in the card image it
reads back. It never calls `_card_write`, so it is not formatting; it is reading and rejecting.

Recover the caller of `0x80068264` in that path and see what it does with the returned 4, then make
`_card_read` answer an unformatted card the way hardware does. The discriminator is unchanged: the
blank-card run's `B0:0x33/0x34/0x36` stop being zero and `A0:0xAB` stops running into the thousands.

Do not re-try the one-shot latch; it is measured and dead.

## Bearing on the user's report

The user reported the save menu not matching the oracle. This is that symptom, reproduced and
narrowed: it is a memory-card lifecycle difference, not a rendering defect -- nothing about the
painter, the ordering table, or widescreen is involved -- and the port currently gets past the card
check only because it formats the card behind the guest's back.
