---
id: 64
title: re_frontier.py set/add silently drops multi-line gap/notes continuation lines
status: open
symptom: a re_frontier.py set or add re-writes docs/re-frontier.md losing every multi-line field line (bullets, indented continuations) with no error
tags: tooling,re-frontier,data-loss
created: 2026-08-16
updated: 2026-08-16
---

## Cause

`tools/re_frontier.py` `load()` only captured the FIRST line of a field
(`- gap:`/`- notes:`) and silently discarded every continuation line: `- PROGRESS …`
bullets and `- NEXT, …` bullets (which match `^- (\w+): ?` with an unrecognized key),
and indented continuations (which match nothing). `save()` then wrote the truncated
value back. A single `set render.own-geometry-family` erased 68 lines of the
`frame.own-render-driver` gap+notes — the whole actor-chain groundwork record — with
`updated render.own-geometry-family` printed as if it had succeeded.

## Fix

`load()` now keeps a `cur_field` and appends any non-blank, non-header, non-field
line inside an entry to the current field's value (verbatim, leading whitespace
included); `serialize()` writes the head line as `- field: …` and the rest verbatim.
A blank line ends the field block. `re_frontier.py --selftest` feeds a fixture whose
multi-line gap/notes MUST survive a load→save→load and exits nonzero if they do not.

## What would falsify / re-open

`re_frontier.py --selftest` failing; or a diff of `docs/re-frontier.md` before/after a
`set`/`add` that deletes a continuation line.
