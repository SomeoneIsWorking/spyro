---
id: 56
title: The producer DB's guest leg cannot attribute anything on spyro: GameConfig::packetPoolBase is 0 (the packet pool is not RE'd)
status: open
symptom: OtAttr span table is empty / producer census reports 0 attributed prims and a span_miss count equal to every prim, on spyro
tags: render,producers,re,gameconfig,blocked
created: 2026-08-11
updated: 2026-08-11
---

## What this blocks

The graphics producer DB (`external/psxport/docs/plans/graphics-producer-db.md`) attributes each drawn
primitive to the guest fn that submitted it by looking the packet's address up in OtAttr's span table.
That table only records stores that land inside the game's PACKET POOL, and the pool range comes from
`GameConfig::packetPoolBase` / `packetPoolStride`.

    spyro/game/core/game_config.cpp:  .packetPoolBase = 0u,  .packetPoolStride = 0u,

Zero is the honest value — this port has not RE'd the pool — but the consequence is that on spyro the
guest leg of the census is **structurally blind**: every prim lands in `span_miss` and no producer row is
ever created from the guest side. That is not "spyro's guest draws nothing".

## Why this is now visible rather than silent

Until 2026-08-11 the framework hardcoded Tomba!2's pool range (`0x800BFE68..0x800E7E68`) in
game-agnostic code, so on spyro the table silently matched nothing and reported no spans — indistinguishable
from "the guest submitted no packets". psxport `21c5f24c` moved the range to GameConfig and made the
un-RE'd case announce itself once:

    [otattr:warn] GameConfig::packetPoolBase/Stride are 0 for this game — packet-pool attribution is
    STRUCTURALLY BLIND here, so an empty span table means 'not measured', NOT 'the guest submitted nothing'.

## What RE'ing it takes

The pool is where the game builds its GP0 packets before `DrawOTag` sends them. Find it from the
submitter side, not by guessing: the OT walk already knows the ordering-table address
(`GpuState::s_ot_madr`), and each OT node's packet words are at the addresses the walk reads — so a
`PSXPORT_DEBUG=ot` run plus `tools/whatis.py` on the addresses the walk touches bounds the region
empirically. Cross-check against the guest's own allocator: whatever global holds the bump pointer
(Tomba!2's is `0x800BF544`) is written every submission, so `PSXPORT_WWATCH=<lo>,<hi>` with
`PSXPORT_WWATCH_BT=1` names the writer.

Fill `packetPoolBase` / `packetPoolStride` with the disassembly that justifies them, per this repo's rule
that every filled GameConfig field carries its evidence. Do NOT copy Tomba!2's values.

## Same for spider1

`spider1/game/core/game_config.cpp` leaves the same two fields 0, with the same consequence.
