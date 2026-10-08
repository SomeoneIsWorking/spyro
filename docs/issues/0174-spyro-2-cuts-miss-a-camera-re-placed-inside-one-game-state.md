---
id: 174
title: Spyro 2 cuts miss a camera re-placed inside one game state
status: open
symptom: a respawn or scripted camera jump is blended across
tags: spyro2,record,cut
created: 2026-10-08
updated: 2026-10-08
---

`spyro2::FrameCut` declares a cut when the game state (0x800681C8) or level id (0x80066F90)
changes. A camera that jumps without either changing is blended across the jump: a respawn inside a
level, or a scripted camera. FUN_8001D8A4 copies the camera from 0x80068064/0x80068070 while byte
0x80067FC9 is set; whether entering, leaving, or a shot change inside that mode re-places the camera
has not been traced, so none of it is declared. Needs: the writers of 0x80068064 and the respawn
path, each traced to the guest event that places the camera.
