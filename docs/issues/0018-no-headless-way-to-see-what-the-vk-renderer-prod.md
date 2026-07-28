---
id: 18
title: No headless way to see what the VK renderer produced — a per-frame readback hangs the port
status: dead-end
symptom: PSXPORT_GPU_DUMP cannot show rasterised geometry (I008), and the only VK readback path (gpu_vk_shot_region) is reachable solely from the interactive REPL — unusable from a batch run or a gate.
tags: gpu,tooling,dead-end
created: 2026-07-28
updated: 2026-07-28
---

ATTEMPTED AND REVERTED. I added a PSXPORT_VK_DUMP=dir[:every] block to gpu_present_ex mirroring
PSXPORT_GPU_DUMP but calling gpu_vk_shot_region. It does not work: with the variable set the port
produces exactly ONE frame in 25s instead of 3931, and writes no files. So the readback BLOCKS on the
first call — it never returns and never logs its own 'wrote' line. vk_path() is confirmed 1 at that
point, so the guard is not the issue.

Reverted rather than shipped. A diagnostic that hangs the program under test is worse than none, and I
do not understand the cause well enough to leave it in — most likely a GPU fence/readback that cannot
complete at that point in the frame, or one that requires a submit that headless mode has not made.

WHY THIS MATTERS: it leaves a real hole. 'Are the pixels correct' is currently unanswerable in a batch
run. The prim-submission count (now gate check 4) proves the guest is DRAWING, which is a different and
weaker claim.

Next thing to try, in order: (1) find out why the readback blocks — compare with how the REPL reaches it,
since the REPL path demonstrably works; (2) if it needs a completed submit, dump AFTER present_window()
rather than before frame_finalize; (3) failing that, a windowed run with a screenshot key is still a
manual answer, but it is an answer.
