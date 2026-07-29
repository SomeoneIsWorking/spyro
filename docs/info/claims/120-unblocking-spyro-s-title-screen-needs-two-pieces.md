---
id: C120
kind: claim
status: holds
created: 2026-07-29
tags: memcard,event,framework
---

## Claim

Unblocking Spyro's title screen needs TWO pieces, and psxport has neither wired for this game. (1) Hle::deliverEvent only MARKS matching slots (ev[i].fired = 1); it never invokes the handler. That is correct for polled EvMdNOINTR events read back via TestEvent, but Spyro opens its memory-card event with mode 0x1000 = EvMdINTR, where the BIOS CALLS the handler. The framework already captures what is needed — HleEvCB stores both mode and func — so this is a missing invocation, not missing state. (2) Nothing delivers HwCARD (0xF4000001) events for this game. psxport does ship a memory-card model (class Memcard, memcard.cpp) but it replaces the BIOS libcard/libmcrd B0-vector frame primitives, and the consuming game installs card_overrides_init to use it. Spyro's port installs no card overrides at all, and Spyro does not use that API: it drives the card through its own driver at 0x80066000-0x80069000, opening HwCARD and running its own SIO state machine. So the BIOS-level model never intercepts Spyro's card access.

## Evidence

hle.cpp:37 Hle::deliverEvent sets only ev[i].fired for open+enabled slots matching class and spec; hle.h:10 HleEvCB = { open, enabled, fired, ev_class, spec, mode, func }, so the handler is stored at OpenEvent (hle.cpp case 0x08) and simply never called. grep of game/core/*.cpp finds no card_overrides_init / card_hle / memcard reference, so Spyro's port installs none. Spyro's own card path was traced this session: OpenEvent(0xF4000001, 4, 0x1000, 0x80067DD0) at 0x80067EA0 (C119), with helpers 0x800663D8 / 0x8006841C / 0x80068FC4 in the 0x80066000-0x80069000 range.

## What would falsify it

A game observed working through psxport's Memcard model while opening HwCARD with EvMdINTR, which would mean delivery already reaches callback-mode handlers by some path not found here.
