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
// per-frame GameConfig group is still 0. Taking over the frame loop is a later, separately-gated
// step that requires RE'ing Spyro's main loop first.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
static void spyro_bootInit(Core* c) {
  rec_dispatch(c, c->cfg->gameMain);
}

// registerOverrides — install this game's native override clusters into the process-global registry.
// Spyro has none yet: every guest function still runs its recompiled body. When the first native
// function is taken over, it registers here (see psxport docs/recomp-overrides).
static void spyro_registerOverrides(Game*) {
  // Game-function ownership goes here, installed into the process-global override registry. Each
  // entry either observes its recompiled body via a super-call (the first step of owning it) or
  // replaces it once the native reimplementation is byte-gated against the substrate.
  spyro_register_cd_queue();
  spyro_register_level_probes();   // TEMPORARY — see docs/issues/0017
  spyro_register_native_rand();    // OWNED natively (not a probe): rand() 0x8006272C
  spyro_register_native_leaves();  // OWNED natively: hot leaves (copy3 / zero3 / fill)
  spyro_register_native_vec();     // OWNED natively: vadd / vsub / angle-table lookup
  spyro_register_native_gte();     // OWNED natively: vector length (GTE SQR + sqrt table)
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
