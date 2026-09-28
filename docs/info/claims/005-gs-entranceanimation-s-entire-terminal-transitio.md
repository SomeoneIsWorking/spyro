---
id: C005
kind: claim
status: holds
created: 2026-09-28
tags: transition,re
depends: titles/spyro1/core/spyro1_transition_skip.h
---

## Claim

GS_EntranceAnimation's entire terminal transition is the single guest store at 0x8002E070 (sw zero, 0x57d8(at) = g_Gamestate = GS_Playing) inside func_8002E000 at 0x8002E000, and it cannot be dispatched in isolation: the store is 0x10 bytes before the epilogue's lw ra, 0x10(sp), which overwrites the return address dispatchGuestToReturn0 installed.

## Evidence

docs/issues/0138

## What would falsify it

a dispatch of 0x8002E06C that returns to its caller, or a run in which ending stage 9 freezes g_Camera.m_Rotation.y, or the GS_Playing arm stops calling CameraUpdate at 0x80033B4C
