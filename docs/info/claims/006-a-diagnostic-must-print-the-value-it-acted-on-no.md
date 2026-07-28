---
id: C006
kind: claim
status: holds
created: 2026-07-28
tags: workflow
---

## Claim

A diagnostic must print the VALUE it acted on, not merely that it ran

## Evidence

The vblank counter was first written as 0x80074C20 (arithmetic slip; correct is 0x800749E0). The counter then read 401217493, so cur>=target always held, the wait returned immediately, and every 'VSync: timeout' vanished — indistinguishable from success. It was caught only because the debug line printed the counter value and '+0 frames'. A handler logging 'vsync ok' would have shipped a no-op wait as a fix.

## What would falsify it

n/a — this is a workflow rule derived from a specific incident; it stops being useful only if diagnostics stop being read
