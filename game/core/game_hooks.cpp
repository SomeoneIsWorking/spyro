// game_hooks.cpp — the Spyro-specific GameHooks vtable (psxport's game_iface.h seam).
//
// GameHooks is how the PSX-generic framework reaches GAME behaviour. Tomba!2 fills most of it with
// its native reimplementation (Engine::frame, stage bodies, native music, ...). Spyro owns nothing
// natively yet, so nearly every hook here is deliberately null.
//
// THIS IS PHASE 0 OF THE PORTING PLAYBOOK (docs/porting-a-new-psx-game.md): stand the recomp up and
// run EVERYTHING on the substrate first, then progressively take ownership function by function,
// each step gated byte-exact against the substrate it replaces. A hook that returned a plausible
// native result before its function had been reverse-engineered would make a broken port LOOK
// finished — the exact failure the playbook calls out. Null is the honest state.
//
// Core tolerates null hooks (it guards every call site) EXCEPT the two the boot path invokes
// unconditionally — bootInit and registerOverrides — which are implemented below.
#include "core.h"
#include "game_iface.h"
#include "spyro_game.h"
#include "cfg.h"      // cfg_on — PSXPORT_NO_NATIVE is a feature flag, not a diagnostic
#include "hostprof.h"
#include <lucent/log.h>
#include "fntrace.h"

// rec_dispatch — the substrate's address->recompiled-function router (core.h, extern "C").
extern "C" void rec_dispatch(Core* c, uint32_t addr);

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// bootInit — what the game does between crt0 and its frame loop.
//
// For Tomba!2 this is a native transcription of the boot prologue. For Spyro it is simply: enter the
// recompiled main() and let the guest run itself. crt0_setup() has already established the guest
// state main() expects (.bss cleared, sp/fp/gp set, heap globals written, libc init dispatched), so
// dispatching gameMain resumes the real program exactly where its crt0 tail-called it.
//
// CONSEQUENCE, stated plainly: Spyro's main() contains the game's OWN frame loop, so this call does
// not return — the framework's native_step_frame loop never runs, and per-frame hooks are never
// reached. That is correct for Phase 0 ("everything dispatched to the substrate") and it is why the
// per-frame GameConfig group is still 0.
//
// PSXPORT_SPYRO_FRAME_LOOP=1 takes that loop over, in GAME code (frame_loop.cpp), which is where it
// has to live: the framework's native_step_frame is unreachable in this port and is shaped for
// Tomba!2's per-frame OT model, not Spyro's (C073). Off by default — the loop is bring-up, and its
// native render branch aborts by design.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
static void spyro_bootInit(Core* c) {
  if (spyro_frame_loop_enabled()) spyro_frame_loop_run(c);   // does not return
  rec_dispatch(c, c->cfg->gameMain);
}

// registerOverrides — install this game's native override clusters into the process-global registry.
//
// The mix here is the honest picture of how far the port has come. MOST entries are OBSERVERS: they
// log and then super-call the recompiled body, so the guest code still does the work. The CD and pad
// ones are platform-level SUPPLY — they provide what the hardware would have and then run the guest
// body. The native_* clusters are the only real OWNERSHIP: their recompiled bodies never run, and
// each is verified byte-exact against the body it replaced on every gate run (PSXPORT_NDIFF).
static void spyro_registerOverrides(Game*) {
  // Game-function ownership goes here, installed into the process-global override registry. Each
  // entry either observes its recompiled body via a super-call (the first step of owning it) or
  // replaces it once the native reimplementation is byte-gated against the substrate.
  hostprof_init();                 // PSXPORT_PROF=1 — host-PC sampling, to pick ownership targets by
                                   // MEASURED time rather than by static caller counts
  spyro_register_cd_queue();
  // PSXPORT_NO_NATIVE=1 — install NO natively-owned bodies, so every call runs the recompiled
  // substrate instead. This is the A/B switch for "is one of our own replacements responsible?",
  // and it is the only way to ask that on a path the per-call differential cannot reach: NDIFF
  // verifies the FIRST N calls of each site, so a body that is wrong only after millions of calls
  // (say, on inputs a later level produces and the title screen never does) is invisible to it.
  // The platform supply above is deliberately NOT gated — removing it changes what the port can do
  // at all, which would confound the comparison.
  if (cfg_on("PSXPORT_NO_NATIVE")) {
    lucent::warn("native", "PSXPORT_NO_NATIVE=1 — native bodies NOT installed; the substrate runs "
                           "everything. Diagnostic only: any behaviour difference from a normal run "
                           "is attributable to a natively-owned body.");
  } else {
  spyro_register_native_rand();    // OWNED natively (not a probe): rand() 0x8006272C
  spyro_register_native_leaves();  // OWNED natively: hot leaves (copy3 / zero3 / fill)
  spyro_register_native_vec();     // OWNED natively: vadd / vsub / angle-table lookup
  spyro_register_native_gte();     // OWNED natively: vector length (GTE SQR + sqrt table)
  spyro_register_native_angle();   // OWNED natively: 8-bit/12-bit angle helpers + the calibrated spin
  spyro_register_native_util();
  spyro_register_native_terrain(); // BRING-UP, off unless PSXPORT_NATIVE_TERRAIN=1: the terrain
                                   // renderer under differential verification (issue 0037)
  spyro_register_wide_clip();      // WIDESCREEN: the guest's own renderers, with the right clip
                                   // bound moved to the wide width (inert at 4:3)
  spyro_register_native_render();  // MEASUREMENT, off unless PSXPORT_NDIFF_IDENTITY=1: can the
                                   // differential validate a renderer at all? (re-frontier
                                   // render.own-geometry-family)    // OWNED natively: strlen / global swaps / dist2d / display-list link
  // LAST, deliberately: fntrace claims the same single override slot the registrations above use, so
  // installing it earlier means the next registration silently displaces it and the trace reports
  // "never called" for a function that runs constantly. Going last makes the collision visible
  // instead — see the hazard note in fntrace.cpp.
  }
  fntrace_init();                  // PSXPORT_FNTRACE=<addr,...> — "did control REACH this function?" 
}

static const GameHooks g_spyro_hooks = {
  /* ctxCreate  */ nullptr,   // no per-Core game context yet — Core tolerates null and leaves gameCtx null
  /* ctxDestroy */ nullptr,

  /* frameUpdate        */ nullptr,   // unreached in Phase 0: the guest owns its own frame loop
  /* drawOTag           */ nullptr,
  /* musicCoordTick     */ nullptr,
  /* cdDialogToneActive */ nullptr,
  /* cdMusicFadeIn      */ nullptr,

  /* audioMixFrame        */ nullptr, // no native music engine — SPU output is the guest's own
  /* audioNowPlayingName  */ nullptr,
  /* audioSoundTestPlay   */ nullptr,

  /* bootInit                */ spyro_bootInit,
  /* schedFreshEntry         */ nullptr,   // no cooperative stage scheduler (Spyro has no overlays/stages)
  /* hasNativeHandlerForEntry*/ nullptr,
  /* registerOverrides       */ spyro_registerOverrides,

  /* renderFadeState    */ nullptr,
  /* replBehaviorName   */ nullptr,
  /* replCamTeleport    */ nullptr,
  /* replCamTeleportOff */ nullptr,
  /* renderBbFrameReset */ nullptr,

  /* replCommand     */ nullptr,
  /* devWarpAreaLoad */ nullptr,
  /* devAreaCount    */ nullptr,   // no dev-warp until Spyro's level table is RE'd
  /* devAreaName     */ nullptr,
  /* devWarpAllowed  */ nullptr,

  /* schedStageBody */ nullptr,
  /* schedRng       */ nullptr,

  /* fps60WorldPass  */ nullptr,   // enhancements come after Phase 1 ownership
  /* fps60BbSwapPrev */ nullptr,

  /* selftestGame */ nullptr,
};

const GameHooks* spyro_game_hooks() { return &g_spyro_hooks; }
