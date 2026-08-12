// producer_run.h — THIS PORT's ownership of the graphics-producer DB's run lifecycle.
//
// WHY A PORT-SIDE FILE EXISTS FOR A FRAMEWORK FEATURE. The DB's two lifecycle calls
// (external/psxport/runtime/recomp/producer_db.h) used to live inside the framework's own frame loop
// (`native_boot.cpp` `game_main`), and THIS PORT NEVER REACHES IT: main.cpp calls `dc_boot_init` and
// never `native_boot_run`, and neither the guest's `main()` (0x80012204, whose epilogue is
// unreachable) nor the port's own `frame_loop.cpp` `run()` (`[[noreturn]]`, unconditional `for(;;)`)
// ever returns. So the DB emitted NOTHING here — no report line, no `scratch/producers/`, no claims
// file — while the census was armed and being fed. Issue #58.
//
// THE HARD PART IS NOT `begin`, IT IS `finish`: this port has no "after the last frame".
// What this file does about that, and what it deliberately does NOT do:
//
//   * A FRAME CAP (`PSXPORT_NATIVE_FRAMES=<n>`) is the primary path. It creates the missing "after
//     the last frame" by ENDING the run deterministically at a frame number the caller chose, then
//     calling `producer_db_finish` on the way out. Same knob, same default (0 = uncapped) and the
//     same meaning as the framework's own headless cap — one presented frame per count, because in
//     this port one delivered vblank field is one present (vsync.cpp). Reusing that name rather than
//     inventing `PSXPORT_SPYRO_FRAMES` keeps the port from growing a private synonym for a knob that
//     already exists and is dead code here.
//   * `atexit` is the SECONDARY path, and it is what makes an interactive session produce a DB: the
//     window close button reaches `exit(0)` (framework `gpu_vk.cpp` on `SDL_EVENT_QUIT`), and
//     `exit(0)` runs atexit handlers. Registered from `begin`, guarded so the cap path and the
//     atexit path cannot both report.
//   * NOT A PERIODIC FLUSH, and this is a correctness reason rather than a taste one:
//     `producer_db_finish` is not idempotent in the way a periodic flush needs. `appendClaims`
//     appends `mClaimCount` addresses to `claims.txt` on EVERY call without deduplicating
//     (producer_census.h), so flushing every N frames would multiply every earned claim by the
//     number of flushes and inflate the next run's claim set — the DB would read as more
//     cross-run evidence than exists. The per-run JSONL name is also only second-resolution
//     (`run-%Y-%m-%dT%H:%M:%S.jsonl`), so two flushes inside one second silently overwrite. Making
//     a flush safe is a FRAMEWORK change (dedup in `appendClaims`, a sequence in the filename);
//     until it lands, a port must call `finish` exactly once.
//   * NOT A SIGNAL HANDLER. `SIGINT`/`SIGTERM` are already owned by the framework watchdog, which
//     answers them with `_exit(130)` after printing the stuck backtrace — the single most useful
//     diagnostic this port has. Stealing that handler to flush a DB would trade a hang diagnosis for
//     a report, and `producer_db_finish` is not async-signal-safe anyway (it `fopen`s, formats, and
//     allocates through lucent). So the honest statement is the one in `begin`'s log line:
//
//         A RUN THAT ENDS BY SIGNAL EMITS NO DB. `timeout -s KILL` (SIGKILL, which no handler can
//         catch) and Ctrl+C both end the process before any report. That includes `tools/gate.sh`,
//         which REQUIRES rc=137 — a gate run can never produce a DB, by construction. Cap the run
//         (`PSXPORT_NATIVE_FRAMES`) or close the window; do not read a missing report as "this game
//         draws nothing".
#pragma once
class Core;

// Once, BEFORE the first frame: loads the accumulated claim set, reads the frame cap, arms the
// atexit fallback, and prints what this run's DB can and cannot emit. Call from main().
void spyro_producer_run_begin(Core* c);

// Once per PRESENTED frame, from the port's real frame boundary (the vblank field in vsync.cpp).
// Ends the run — report, JSONL, claim append, `exit(0)` — when the frame cap is reached.
void spyro_producer_run_frame(Core* c);
