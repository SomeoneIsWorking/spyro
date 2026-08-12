---
id: 58
title: The producer DB emits NOTHING in this port — the census lifecycle was unreachable, and it fails SILENTLY
status: open
symptom: no [producers] run-end line, no scratch/producers/ directory, no claims.txt, even though the census is armed and fed
tags: producers,census,verification,instrument
created: 2026-08-12
updated: 2026-08-12
---

MEASURED 2026-08-12 by adversarial verification of a claim I had made in the pin-bump commit ('bumping is all this repo needs'). Three separate things are missing, in this order:

(a) THE LIFECYCLE WAS UNREACHABLE. The DB's load/report/persist calls lived inside game_main in the framework's native_boot.cpp, and THIS PORT NEVER REACHES IT: game/core/main.cpp calls dc_boot_init and never native_boot_run, and game/core/frame_loop.cpp run() is [[noreturn]] with an unconditional for(;;). Verified: scratch/producers/ does not exist in this repo after a 90s headless run, while Tomba2Engine/scratch/producers/ holds today's runs as the positive control. FIXED FRAMEWORK-SIDE (psxport 240d8f9a): producer_db_begin(c) / producer_db_finish(c) in producer_db.h are now port-callable. THIS PORT STILL HAS TO CALL THEM — two lines, in frame_loop.cpp around the loop. Until it does, the DB produces nothing here.

(b) THE GUEST LEG IS STRUCTURALLY BLIND. game/core/game_config.cpp:79-80 has packetPoolBase = 0, packetPoolStride = 0, so pool_range() returns known=false and OtAttr records no spans. The framework DOES say so out loud ('packet-pool attribution is STRUCTURALLY BLIND here'), which is the one part of this that was never silent. Un-blinding it needs the packet pool RE'd.

(c) NO NATIVE PRODUCER IS DECLARED. Zero hits for ProducerScope / pc_producer anywhere in this tree, and the native render-queue chokepoint has exactly ONE game call site (game/render/fx_title_menu.cpp:215 push2dQuad); game/core/vsync.cpp:229 says in its own comment that this repo never calls drawWorldQuad. Measured with PSXPORT_DEBUG=unscoped over a 75s run: 0 undeclared-native-prim lines, i.e. the native leg pushed nothing through the chokepoint at all.

ORDER MATTERS: (a) first, or the other two cannot be observed — with no report there is no way to see that the guest leg is blind or that no producer is declared. Do NOT quote 'the framework does this automatically' for this port until (a) is called here.
