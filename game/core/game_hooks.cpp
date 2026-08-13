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
#include "fx_paired_actor.h"
#include "boot_skip.h"
#include <cstring>

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
// Tomba!2's per-frame OT model, not Spyro's (C073). Off by default — the loop is bring-up, and the
// render seam it calls (game/render/) aborts by design on its native leg, which is the default leg
// (`PSXPORT_RENDER_PSX=1` selects the reference one).
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

// Spyro's scene renderer (0x80022A2C) reads these globals before walking any objects: five packed
// GTE rotation words at 0x80076DD0 and the camera's world position at 0x80076DF8. It subtracts the
// camera position from each world position and then applies the matrix, so the equivalent affine
// view transform is R * world + (-(R * cameraPosition) / 4096). These are persistent game camera
// globals, not a readback of whichever per-object matrix happens to be in the GTE.
static constexpr uint32_t kSceneRotation = 0x80076DD0u;
static constexpr uint32_t kCameraPosition = 0x80076DF8u;

static void spyro_decode_scene_cam(const uint32_t packed[5], const int32_t camera[3],
                                   float R[3][3], float T[3]) {
  R[0][0] = (int16_t)packed[0];         R[0][1] = (int16_t)(packed[0] >> 16);
  R[0][2] = (int16_t)packed[1];         R[1][0] = (int16_t)(packed[1] >> 16);
  R[1][1] = (int16_t)packed[2];         R[1][2] = (int16_t)(packed[2] >> 16);
  R[2][0] = (int16_t)packed[3];         R[2][1] = (int16_t)(packed[3] >> 16);
  R[2][2] = (int16_t)packed[4];
  for (int row = 0; row < 3; row++) {
    const double dot = (double)R[row][0] * camera[0] +
                       (double)R[row][1] * camera[1] +
                       (double)R[row][2] * camera[2];
    T[row] = (float)(-dot / 4096.0);
  }
}

static void spyro_fps60ReadSceneCam(Core* c, float R[3][3], float T[3]) {
  uint32_t packed[5];
  int32_t camera[3];
  for (int i = 0; i < 5; i++) packed[i] = c->mem_r32(kSceneRotation + (uint32_t)i * 4u);
  for (int i = 0; i < 3; i++) camera[i] = (int32_t)c->mem_r32(kCameraPosition + (uint32_t)i * 4u);
  spyro_decode_scene_cam(packed, camera, R, T);
}

static int spyro_selftestGame(const char* which, const char*) {
  if (std::strcmp(which, "pairedpose") == 0) return spyro_paired_actor_selftest();
  if (std::strcmp(which, "bootskip") == 0) return spyro_boot_skip_selftest();
  if (std::strcmp(which, "scenecam") != 0) return 2;

  float R[3][3], T[3];
  const uint32_t identity[5] = {0x00001000u, 0u, 0x00001000u, 0u, 0x00001000u};
  const int32_t p0[3] = {10, 20, 30};
  spyro_decode_scene_cam(identity, p0, R, T);
  int checks = 0;
  auto expect = [&](bool pass, const char* what) {
    checks++;
    if (!pass) lucent::error("selftest", "FAIL(scenecam): {}", what);
    return pass;
  };
  bool ok = true;
  ok &= expect(R[0][0] == 4096 && R[1][1] == 4096 && R[2][2] == 4096,
               "identity diagonal unpack");
  ok &= expect(R[0][1] == 0 && R[0][2] == 0 && R[1][0] == 0 && R[1][2] == 0 &&
               R[2][0] == 0 && R[2][1] == 0, "identity off-diagonal unpack");
  ok &= expect(T[0] == -10 && T[1] == -20 && T[2] == -30,
               "identity camera-position translation");

  // +90 degrees around Z: R*(2,3,5)=(-3,2,5), hence view T=(3,-2,-5).
  const uint32_t rot_z[5] = {0xF0000000u, 0x10000000u, 0u, 0u, 0x00001000u};
  const int32_t p1[3] = {2, 3, 5};
  spyro_decode_scene_cam(rot_z, p1, R, T);
  ok &= expect(R[0][1] == -4096 && R[1][0] == 4096 && R[2][2] == 4096,
               "signed packed rotation unpack");
  ok &= expect(T[0] == 3 && T[1] == -2 && T[2] == -5,
               "rotated camera-position translation sign");

  if (ok) lucent::info("selftest", "PASS(scenecam): {} checks", checks);
  return ok ? 0 : 1;
}

// Bind by name. A positional table silently shifted when psxport inserted devWarpAreaEnter because
// the surrounding null callbacks were type-compatible; the first later non-null callback merely
// made that old defect visible. Unlisted hooks are value-initialised to null, so this is also the
// exact inventory of framework callbacks Spyro has actually stood up.
static const GameHooks g_spyro_hooks = {
  .bootInit           = spyro_bootInit,
  .registerOverrides  = spyro_registerOverrides,
  .selftestGame       = spyro_selftestGame,
  .fps60ReadSceneCam  = spyro_fps60ReadSceneCam,
};

const GameHooks* spyro_game_hooks() { return &g_spyro_hooks; }
