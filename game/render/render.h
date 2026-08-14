// render.h — Spyro's RENDER SEAM: the one place the port decides where the picture comes from.
//
// SHAPE TAKEN FROM Tomba!2 (Tomba2Engine game_tomba2.cpp `Engine::drawOTag` + its game/render/):
//
//     if (mode.psxRender()) { <substrate OT walk>; return; }   // the reference path
//     renderScene();                                            // the native producers
//     rq.flush();                                               // …emitted here
//
// The two legs differ ONLY in where the picture comes from. Everything else about the frame — the
// update, the frame step, the input-latch window, present and pace — is the same code either way
// (game/core/frame_loop.cpp, game/core/vsync.cpp).
//
// THE MODE IS THE FRAMEWORK'S, NOT A SPYRO FLAG. `Core::rsub.mode` (psxport runtime/recomp/
// render_mode.h) is per-Core and already means exactly this in every consumer of the framework: false
// = the native path, true = the PSX recomp reference. SBS and dualcore SET it that way when they
// configure their two cores (sbs.cpp M_RENDER: core A native, core B PSX), so a game that inverted
// the sense would silently give an SBS core configured for "native" the reference picture — the
// wrong-source failure this project keeps paying for. Hence: no PSXPORT_SPYRO_* render switch;
// `PSXPORT_RENDER_PSX=1` selects the reference, exactly as it does for Tomba!2.
#pragma once
#include "fx_paired_actor.h"
#include <cstdint>
class Core;

// The stage-selector value whose arm has a native producer today (game/render/fx_title_menu.cpp).
// Named here because the seam's dispatch and the producer's own comments both have to agree on it.
constexpr uint32_t kStageFrontEnd = 13u;

// The guest's per-frame RENDER DRIVER, called once per drawn frame from its main() 0x80012204 at
// 0x8001227C. It resets the OT/packet pool, dispatches on the stage selector below, and ends in the
// display tail (DrawSync, the >=2-vblank throttle, PutDispEnv/PutDrawEnv, DrawOTag). It IS the
// reference path — see re-frontier step `frame.own-render-driver` for the order it comes apart in.
constexpr uint32_t kFrameRenderDrv = 0x8001ED5Cu;

// ── One arm of the guest's render driver 0x8001ED5C ──────────────────────────────────────────────
struct StageArm {
  uint32_t stage;
  uint32_t handler;      // 0 when the arm dispatches indirectly or picks between two handlers
  const char* what;
};

// ── One layer of the FIELD (stage 0) arm, in the guest's own draw order ──────────────────────────
struct FieldLayer {
  uint32_t fn;
  uint32_t gate;        // 0 = unconditional
  bool gateNonZero;     // true: runs when [gate] != 0; false: runs when [gate] == 0
  const char* what;
};

// ── The scene a renderer is being asked to produce ───────────────────────────────────────────────
// `arm` is null when the stage selector is outside 0..15 — the guest's if-chain draws nothing for
// such a value, so a null arm is a real answer ("nothing to port here"), not a lookup failure.
struct Scene {
  uint32_t stage;
  const StageArm* arm;
};

// class SpyroRenderer — the render seam for ONE frame on ONE core.
//
// Constructed per frame on the stack; its paired-producer census is therefore frame- and core-local.
class SpyroRenderer {
public:
  explicit SpyroRenderer(Core* c) : mC(c) {}

  // Read PSXPORT_RENDER_PSX into the framework's per-Core RenderMode. Called once, from the frame
  // loop's entry — see the .cpp for why the framework's own parse of that flag never runs here.
  static void installModeFromConfig(Core* c);

  // ONE frame's picture: the reference OT walk, or the native producers.
  void drawFrame();

  // WHICH SCENE the game is drawing right now, from the guest's own stage selector. A read of game
  // state, used both to dispatch a native producer and — on the reference leg — to report what the
  // porting backlog actually consists of on a real run.
  Scene classifyScene() const;

private:
  void referenceOtWalk() const;                          // the guest's render driver, unmodified
  void renderScene(const Scene& sc) const;               // the native picture — dispatches producers
  [[noreturn]] void abortUnimplemented(const Scene& sc, const char* why) const;
  void reportBacklog(const Scene& sc) const;             // scene.cpp — the arm/layer detail

  // ── PRODUCERS ─────────────────────────────────────────────────────────────────────────────────
  // game/render/fx_title_menu.cpp — stage 13's front-end sprite layer (guest 0x8007CD38's picture,
  // driven by 0x8007CEE4's own state machine). False = this frame's menu mode has no producer, so
  // the seam must abort rather than present the scene without its menu.
  bool titleMenuRender(int32_t drawOfsX, int32_t drawOfsY,
                       int32_t clipX0, int32_t clipY0, int32_t clipX1, int32_t clipY1) const;
  void titleMenuBacklogReport() const;                   // what stage 13 still owes, once per run
  bool spriteEmit(int32_t x, int32_t y, int32_t id, uint32_t style,
                  int32_t drawOfsX, int32_t drawOfsY,
                  int32_t clipX0, int32_t clipY0, int32_t clipX1, int32_t clipY1) const;
  // fx_sprite_queue.cpp — native screen-space class of RasterizeSpritePrimQueue 0x80022A2C.
  // Also owns stage-13/mode-3's text-actor construction. False means the handler's separate
  // paired-actor pass 0x80023AC4 is armed; its normal opaque/textured group is owned.
  bool stage13Mode3Render() const;

  Core* mC;
  // The DRAWENV this frame is being drawn with, set by drawFrame()'s call to nativeFrameBegin() on
  // the native leg only. 0 on the reference leg, where the guest's own driver owns the env.
  uint32_t mEnv = 0;
  mutable SpyroPairedActorFrameState mPaired{};
};
