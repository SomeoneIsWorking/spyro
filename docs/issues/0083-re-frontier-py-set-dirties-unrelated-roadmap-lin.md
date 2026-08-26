---
id: 83
title: re_frontier.py set dirties unrelated roadmap lines
status: resolved
symptom: one scoped frontier update adds trailing spaces to every empty field and an extra blank line at EOF, so git diff --check fails on unrelated entries
tags: tooling,re-frontier,workflow
created: 2026-08-26
updated: 2026-08-26
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-26)
Entry.serialize always emitted a space after every field colon even when the value was empty, and save appended two newlines after the final entry. Serialization now conditionally emits the separator, assembles sections before one write, and guarantees exactly one final newline. The expanded self-test verifies multi-line value preservation, no trailing whitespace, and no EOF blank line; Ruff passes and a real set followed by git diff --check is clean.
