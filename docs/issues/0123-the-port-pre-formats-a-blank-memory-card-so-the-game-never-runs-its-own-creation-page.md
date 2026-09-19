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

## What is actually open

The guest call Spyro makes in title sub_state 12 while the reference is drawing `CREATING SAVE
FILE...` is unidentified. Finding it is the next step, and it needs a card-channel trace from the
product itself:

* `card_overrides_init` enables `Memcard::setVerbose` when the lucent channel `card` is on, and every
  B0 file handler then logs its function and arguments.
* `tools/picture_oracle.py --product-env PSXPORT_DEBUG=card` does NOT surface those lines -- the
  product's own output is not captured on that path. Drive the product alone
  (`tools/drive.py`, or the binary through `external/psxport/tools/port/launch_environment.py::
  agent_environment`) with `PSXPORT_DEBUG=card`, hold it in the save menu, and read which B0 function
  is called and what it returns.

Do not implement another handler before that trace names one. Two guesses have now cost a build and
a run each.

## Bearing on the user's report

The user reported the save menu not matching the oracle. This is that symptom, reproduced and
narrowed: it is a memory-card lifecycle difference, not a rendering defect -- nothing about the
painter, the ordering table, or widescreen is involved -- and the port currently gets past the card
check only because it formats the card behind the guest's back.
