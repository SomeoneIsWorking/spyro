---
id: 22
title: GameConfig grows upstream and silently breaks consumers' positional initializers
status: open
symptom: Build fails with 'too many braces around scalar initializer for type uint32_t' in game/core/game_config.cpp — an error that names neither the added field nor the struct.
tags: framework,workflow
created: 2026-07-28
updated: 2026-07-28
---

Happened TWICE in one session. Upstream added `discEnvVar` (a const char*), then `bootFmv[4]`; each
time this port's positional GameConfig initializer silently shifted and the build broke with a message
that points at the wrong thing entirely.

Worse than the breakage is the failure mode around it. The first time, my verify step was
`cmake --build ... | grep -c error` — I read the COUNT without acting on it, the gate then ran a STALE
binary, and I quoted its numbers in a commit. Those numbers happened to be correct, which is luck, not
diligence. Counting errors instead of reading them is the same shape as the gate that counted log lines
while the port segfaulted.

TWO FIXES:
  1. Consumer side: DESIGNATED initializers (`.discEnvVar = ...`). C++20 is already in use, and the
     positional comments in game_config.cpp already carry every field name, so the conversion is
     mechanical. Then a new upstream field is simply absent-and-zero rather than a silent shift.
     Entirely in this repo's hands.
  2. Framework side: GameConfig could carry a size/version static_assert so a mismatch names the field
     that appeared instead of saying "too many braces". Worth proposing, but (1) makes it unnecessary
     here.

Until (1) lands, treat any upstream psxport rebase as build-breaking by default, and READ the build
output rather than a count of it.
