---
id: 176
title: Spyro 2's guest-drawn objects have no producer
status: open
symptom: mobys, HUD, sky and particles are unkeyed; the census cannot rank their submitters
tags: spyro2,record,census
created: 2026-10-08
updated: 2026-10-08
---

Census at shutdown on the 4:3 route: 977,048 primitives, 434,692 keyed (all terrain, 0x80023BB4),
3,644 unkeyed attributed, 538,712 span-miss, duplicated keys 0. The guest submitters below the
draw cannot be ranked: the driver's logic-frame clock (`OtAttr::beginLogicFrame` at the frame tail)
drops the packet spans between the step that builds a table and the step that walks it (issue
0177), so the walk finds no span. Without that clock the spans are never dropped and reused pool
words are charged to stale writers (93,097 unkeyed packets charged to the terrain producer, which
keys every packet it writes). The known guest drawers are the moby drawers (FUN_80044504,
FUN_80046FD8, close mobys FUN_800499D4), the HUD FUN_80053E78, the sky and the particles. Each is one
guest call that loops over its own list (re-frontier `guest.moby-provenance`), so a key per object
needs that drawer native.
