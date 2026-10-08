---
id: 96
title: MemCardExist and MemCardAccept ownership
status: resolved
symptom: the next dependency-ready libmcrd parents were reached but had no native owner
tags: ownership,memcard,frontier
created: 2026-08-28
updated: 2026-08-28
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-28)
Both dependency-ready libmcrd request starters are reached on the current title path: MemCardAccept 0x800665B8 was reached twice and MemCardExist 0x8006635C 346 times in the 1,200-frame trace. Their native owners preserve the binary's idle/busy transaction, callback addresses, operation codes, phase/result reset, request argument, printf child, and event-stack child. The focused contract test passes; dynamic-runtime parity remains unverified until the Lightrec backend is available.
