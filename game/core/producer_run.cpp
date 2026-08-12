// producer_run.cpp — the producer DB's run lifecycle for a port that owns its frame loop.
// See producer_run.h for WHY the frame cap is the mechanism and why a periodic flush, a signal
// handler, and a REPL command were each rejected.
#include "producer_run.h"
#include "core.h"
#include "cfg.h"          // cfg_int — PSXPORT_NATIVE_FRAMES is a run-shape flag, not a diagnostic
#include "producer_db.h"  // framework: producer_db_begin / producer_db_finish
#include <lucent/log.h>
#include <stdlib.h>       // exit, atexit

namespace {

// SANCTIONED atexit EXCEPTION, the same one the framework's overlay_router.cpp documents: an atexit
// handler takes no argument, so the Core it must report on has to be reachable from file scope. Set
// once, in begin(). This port has exactly one Core (main.cpp: one Game, one Core).
Core* s_core = nullptr;

// The cap, in PRESENTED frames. 0 == uncapped (the default, and the only value that leaves every
// existing measurement tool in this repo behaving as it did — tools/gate.sh in particular REQUIRES
// the run to still be alive at its timeout, so a default cap would break the gate).
int  s_cap = 0;
long s_frames = 0;

// finish() exactly once — see producer_run.h: appendClaims does not deduplicate, so a second call
// would double every earned claim in claims.txt.
bool s_finished = false;

void finish_once(const char* why) {
  if (s_finished || !s_core) return;
  s_finished = true;
  lucent::info("producers", "run ending after {} presented frame(s) ({}) — writing the DB",
               s_frames, why);
  producer_db_finish(s_core);
}

void finish_atexit() {
  finish_once("process exit");
  // Same reason as the cap path below: a clean exit that leaves host_turn's file-scope std::thread
  // joinable aborts in std::thread::~thread(). This handler is registered from main(), so it runs
  // BEFORE that static destructor and the join lands in time. Idempotent (shutdown returns early
  // when the thread is not joinable), so the cap path calling it first is fine.
  rec_host_turn_shutdown();
}

}  // namespace

void spyro_producer_run_begin(Core* c) {
  s_core = c;
  producer_db_begin(c);
  s_cap = cfg_int("PSXPORT_NATIVE_FRAMES", 0);
  atexit(&finish_atexit);
  // THE NEGATIVE, PRINTED AT BOOT RATHER THAN INFERRED FROM A MISSING FILE. The whole of issue #58
  // was that the DB's absence was indistinguishable from "this game draws nothing the DB can see", so
  // every run now says up front which of the two endings it is set up for.
  if (s_cap > 0)
    lucent::info("producers", "DB lifecycle armed: this run ends itself after {} presented frame(s) "
                              "and writes the report + JSONL + claim append then. (PSXPORT_NATIVE_FRAMES)",
                 s_cap);
  else
    lucent::warn("producers", "DB lifecycle armed but this run is UNCAPPED, so it will emit a DB ONLY "
                              "if it exits cleanly (window close -> exit(0)). A run killed by signal — "
                              "any `timeout`-bounded headless run, Ctrl+C, and tools/gate.sh, which "
                              "requires rc=137 — writes NOTHING, and that silence is NOT evidence that "
                              "the game draws nothing. Set PSXPORT_NATIVE_FRAMES=<n> to end the run at "
                              "frame n and get the report.");
}

void spyro_producer_run_frame(Core* c) {
  (void)c;   // the Core is latched in begin(); this argument keeps the call site self-documenting
  ++s_frames;
  if (s_cap <= 0 || s_frames < (long)s_cap) return;
  finish_once("frame cap reached");
  // STOP THE HOST-TURN TIMER THREAD BEFORE EXITING, and this is not housekeeping — without it the
  // process ABORTS instead of exiting. MEASURED on the first capped run: the DB was written, then
  // `terminate called without an active exception` and rc=139, with `std::thread::~thread()` on the
  // watchdog's backtrace. `host_turn.cpp`'s timer thread is a FILE-SCOPE `std::thread` (s_thread) that
  // is still joinable at exit, and destroying a joinable thread calls std::terminate. The framework
  // already ships the right answer — `rec_host_turn_shutdown()` (core.h), which stops the timer and
  // joins — and it had ZERO callers anywhere in the framework or in any port, because until now no
  // psxport port had ever reached a clean exit at all. So this is the framework's own shutdown API
  // being called by the first code that needs it, not a workaround for it.
  rec_host_turn_shutdown();
  // exit(), not _exit(): every other atexit-registered reporter (the override registry dump, the SPU
  // WAV finalizer) gets to run, which is what a capped measurement run wants. finish_once has already
  // run, and its guard makes the atexit re-entry a no-op.
  //
  // ONE OF THEM STILL DOES NOT FIRE, and it is the same defect class as the one this file fixes:
  // MEASURED on the first clean-exit run this port has ever had, the config env audit printed
  // NOTHING, at boot or at exit. `report_exit_audit` is armed by `psx::config::report_once()`, whose
  // only caller is `cfg_dump()`, whose only caller is `native_boot.cpp:618` — framework frame-loop
  // code this port never enters. So the audit is not merely killed before it prints here (which is
  // what its own comment says happens), it was never registered. Not fixed here because a port must
  // not decide the framework's config-reporting lifecycle from inside its DB hook; reported instead.
  exit(0);
}
