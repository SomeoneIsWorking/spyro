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

## What is actually open

Make `_card_info` report the card's real state instead of unconditional success, then remove the
pre-format and confirm the product draws `CREATING SAVE FILE...` and reaches the picker on its own.
Two things are still unknown and should be measured before the handler is written:

* which event spec Spyro is waiting on -- `deliverError` is the documented failure channel, but
  whether Spyro's poll loop is watching for it here is not yet measured;
* whether `_card_load` (3 calls in the stalling run, 2 in the control) carries the same answer and
  needs the same treatment.

The discriminator is the histogram above: the fix is right when the unformatted run's `B0:0x33/0x34/
0x36` stop being zero and `A0:0xAB` stops running into the thousands.

## Bearing on the user's report

The user reported the save menu not matching the oracle. This is that symptom, reproduced and
narrowed: it is a memory-card lifecycle difference, not a rendering defect -- nothing about the
painter, the ordering table, or widescreen is involved -- and the port currently gets past the card
check only because it formats the card behind the guest's back.
