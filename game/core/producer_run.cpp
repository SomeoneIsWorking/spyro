// producer_run.cpp — the producer DB's run lifecycle for a port that owns its frame loop.
// See producer_run.h for WHY the frame cap is the mechanism and why a periodic flush, a signal
// handler, and a REPL command were each rejected.
#include "producer_run.h"
#include "cfg.h" // cfg_int — PSXPORT_NATIVE_FRAMES is a run-shape flag, not a diagnostic
#include "core.h"
#include "field_environment_oracle.h"
#include "paired_actor_temporal_evidence.h" // shipping fps60 presenter run proof
#include "producer_db.h"                    // framework: producer_db_begin / producer_db_finish
#include "render_stats.h" // framework: render_depth_coverage_report — whole-run depth denominator
#include "spyro_game.h"   // optional sprite-queue census has the same run-end truth boundary
#include <cstdio>
#include <lucent/log.h>
#include <stdlib.h> // exit, atexit

namespace {

// SANCTIONED atexit EXCEPTION, the same one the framework's overlay_router.cpp documents: an atexit
// handler takes no argument, so the Core it must report on has to be reachable from file scope. Set
// once, in begin(). This port has exactly one Core (main.cpp: one Game, one Core).
Core *s_core = nullptr;

// The cap, in PRESENTED frames. 0 == uncapped (the default, and the only value that leaves every
// existing measurement tool in this repo behaving as it did — a run the gate kills at its
// wall-clock timeout must still be alive then, so a default cap would change that behaviour).
int s_cap = 0;
long s_frames = 0;

// finish() exactly once — see producer_run.h: appendClaims does not deduplicate, so a second call
// would double every earned claim in claims.txt.
bool s_finished = false;

void finish_once(const char *why) {
  if (s_finished || !s_core) {
    return;
  }
  s_finished = true;
  lucent::info(
      "producers", "run ending after {} presented frame(s) ({}) — writing the DB", s_frames, why);
  spyro_paired_actor_temporal_finish(s_core);
  spyro_sprite_queue_census_finish();
  spyro_world_census_finish(s_core);
  spyro_world_animation_oracle_finish();
  spyro_world_native_finish();
  spyro_field_environment_oracle_finish();
  // Whole-run depth coverage, with its denominator (framework render_stats.h). This is THE number
  // the widescreen/60fps discriminator rides on, and the per-frame `ndepth` line could never answer
  // it — it sampled one frame in sixty and printed the same "0.0%" for "no 3D" and "nothing
  // counted" (instrument I041, distrusted for exactly that).
  render_depth_coverage_report(s_core, why);
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

} // namespace

void spyro_producer_run_begin(Core *c) {
  s_core = c;
  spyro_world_animation_oracle_snapshot(c);
  producer_db_begin(c);
  s_cap = cfg_int("PSXPORT_NATIVE_FRAMES", 0);
  atexit(&finish_atexit);
  // THE NEGATIVE, PRINTED AT BOOT RATHER THAN INFERRED FROM A MISSING FILE. The whole of issue #58
  // was that the DB's absence was indistinguishable from "this game draws nothing the DB can see",
  // so every run now says up front which of the two endings it is set up for.
  if (s_cap > 0) {
    lucent::info("producers",
                 "DB lifecycle armed: this run ends itself after {} presented frame(s) "
                 "and writes the report + JSONL + claim append then. (PSXPORT_NATIVE_FRAMES)",
                 s_cap);
  } else {
    lucent::warn("producers",
                 "DB lifecycle armed but this run is UNCAPPED, so it will emit a DB ONLY "
                 "if it exits cleanly (window close -> exit(0)). A run killed by signal — "
                 "any `timeout`-bounded headless run or Ctrl+C — writes NOTHING, and that "
                 "silence is NOT evidence that the game draws nothing. Set "
                 "PSXPORT_NATIVE_FRAMES=<n> to end the run at frame n and get the report "
                 "(the gate's `boot` run does exactly this).");
  }
}

void spyro_producer_run_frame(Core *c) {
  ++s_frames;
  // Semantic-state capture shared by BOTH render legs. Fixed PRESENT indices cannot compare this
  // scene: unchanged runs placed timer 171 anywhere from present 3858 to 4094. This hook is after
  // the title field boundary and fires once on the game's own stage/mode/state/timer tuple.
  if (const int shot_timer = cfg_int("PSXPORT_SPRITE_QUEUE_SHOT_TIMER", -1); shot_timer >= 0) {
    static bool shot_done = false;
    const int32_t timer = (int32_t)c->mem_r32(0x80078D80u);
    if (!shot_done && c->mem_r32(0x800757D8u) == 13u && c->mem_r32(0x80078D78u) == 3u &&
        c->mem_r32(0x80078D7Cu) == 2u && timer == shot_timer) {
      shot_done = true;
      void gpu_vk_present_shot(Core *, const char *);
      char path[128];
      snprintf(path, sizeof path, "scratch/screenshots/spriteq_timer_%d.ppm", shot_timer);
      gpu_vk_present_shot(c, path);
      lucent::info("spriteq",
                   "semantic capture: present={} stage=13 mode=3 state=2 timer={} -> {}",
                   s_frames,
                   timer,
                   path);
    }
  }
  if (s_cap <= 0 || s_frames < (long)s_cap) {
    return;
  }
  spyro_producer_run_end_now("frame cap reached");
}

// END THE RUN CLEANLY, from wherever asks. The frame cap was the only caller of this shutdown for a
// while, which meant every OTHER way of stopping the port — the REPL, and so every screenshot
// capture — died by SIGKILL with the run-end reporters never reached. That is not a tidiness
// problem: those reporters are where the run says which bodies actually executed, so a capture that
// cannot reach them produces a picture nobody can attribute. `end` in the REPL now comes here.
void spyro_producer_run_end_now(const char *why) {
  finish_once(why);
  // STOP THE HOST-TURN TIMER THREAD BEFORE EXITING, and this is not housekeeping — without it the
  // process ABORTS instead of exiting. MEASURED on the first capped run: the DB was written, then
  // `terminate called without an active exception` and rc=139, with `std::thread::~thread()` on the
  // watchdog's backtrace. `host_turn.cpp`'s timer thread is a FILE-SCOPE `std::thread` (s_thread)
  // that is still joinable at exit, and destroying a joinable thread calls std::terminate. The
  // framework already ships the right answer — `rec_host_turn_shutdown()` (core.h), which stops the
  // timer and joins — and it had ZERO callers anywhere in the framework or in any port, because
  // until now no psxport port had ever reached a clean exit at all. So this is the framework's own
  // shutdown API being called by the first code that needs it, not a workaround for it.
  rec_host_turn_shutdown();
  // exit(), not _exit(): every other atexit-registered reporter (the override registry dump, the
  // SPU WAV finalizer) gets to run, which is what a capped measurement run wants. finish_once has
  // already run, and its guard makes the atexit re-entry a no-op.
  //
  // ONE OF THEM STILL DOES NOT FIRE, and it is the same defect class as the one this file fixes:
  // MEASURED on the first clean-exit run this port has ever had, the config env audit printed
  // NOTHING, at boot or at exit. `report_exit_audit` is armed by `psx::config::report_once()`,
  // whose only caller is `cfg_dump()`, whose only caller is `native_boot.cpp:618` — framework
  // frame-loop code this port never enters. So the audit is not merely killed before it prints here
  // (which is what its own comment says happens), it was never registered. Not fixed here because a
  // port must not decide the framework's config-reporting lifecycle from inside its DB hook;
  // reported instead.
  exit(0);
}

long spyro_producer_run_present_count() {
  return s_frames;
}
