// vsync.cpp — Spyro's vblank timebase.
//
// THE PROBLEM. libetc's VSync(mode) waits until the vblank counter at 0x800749E0 reaches a target,
// via the helper at 0x8005DD0C: `while ([0x800749E0] < a0) spin;` with an (a1<<15)-iteration budget,
// printing "VSync: timeout" when it runs out. On real hardware that counter is incremented by the
// VSync IRQ callback. This runtime raises no IRQs, so the counter is frozen at its initial value and
// EVERY VSync times out — which is what the boot log showed.
//
// WHY THIS CHOKEPOINT, and not VSync itself. VSync computes a return value from its own state (the
// GPU status mirror at [[0x800738BC]] and the hblank delta [[0x800738C0]]-[0x800738C4]), then polls
// GPU status after waiting. Reimplementing all of that natively would mean guessing a contract that
// the guest already implements correctly. The helper's contract, by contrast, is unambiguous:
// "do not return until the counter has reached a0". So we supply ONLY the thing the missing IRQ
// would have supplied — the counter advancing over time — and VSync's own logic, its return value
// and its GPU polling all run unmodified on the real recompiled body. That keeps the RE frontier
// honest: nothing here reimplements game or library behaviour, it restores a hardware timebase.
//
// WHY PRESENT HERE. One vblank waited == one frame displayed. The guest still owns its own frame
// loop in this phase (see game_hooks.cpp), so this wait is the only place per frame where the port
// knows a frame boundary has been reached. Presenting and pacing here is what gives the port its
// timebase; when the native frame loop later takes ownership, THAT loop owns present/pace and this
// handler goes back to being a pure counter advance.
#include "core.h"
#include "game.h"
#include "platform_hle.h"
#include "cfg.h"
#include "spyro_game.h"
#include "hle.h"      // Hle::deliverEvent — per-frame BIOS events

namespace {

// The libetc vblank counter. Derived from the wait helper's own body: it compares
// `[0x800749E0] < a0` as its loop condition: lui 0x8007 (=0x80070000) + lw offset 18912 (=0x49E0).
constexpr uint32_t kVblankCounter = 0x800749E0u;

// The wait helper, 0x8005DD0C: wait(a0 = target count, a1 = timeout budget >> 15).
constexpr uint32_t kVblankWait = 0x8005DD0Cu;

// A single wait should never span more than a fraction of a second of vblanks. A far-future target
// means we misread the counter or the guest asked for something unreasonable; advancing to it
// blindly would spin out thousands of frames and look like a hang. Cap it, and SAY so — silently
// clamping is how a wrong timebase hides.
constexpr int kMaxVblanksPerWait = 300;   // ~5s at 60Hz

void vblank_wait(Core* c) {
  const int32_t target = (int32_t)c->r[4];
  int32_t cur = (int32_t)c->mem_r32(kVblankCounter);

  int advanced = 0;
  while (cur < target) {
    if (advanced >= kMaxVblanksPerWait) {
      cfg_logw("vsync", "wait target %d is %d vblanks ahead of the counter (%d) — clamped at %d. "
                        "Either the counter address is wrong or a caller asked for an implausible "
                        "wait; the timebase is suspect either way.",
               target, target - cur, cur, kMaxVblanksPerWait);
      cur = target;   // satisfy the caller rather than hang, but the warning above is the real output
      break;
    }
    // One vblank = one displayed frame. present() puts the guest's drawn frame on screen; pace()
    // holds real time to the frame interval so the game runs at its intended speed rather than
    // spinning as fast as the host can.
    gpu_present(c);
    gpu_pace_frame(c);
    // Deliver the per-frame IRQ-driven BIOS events. The framework normally does this in
    // native_step_frame — but that loop NEVER RUNS here, because the guest still owns its own frame
    // loop (game_hooks.cpp). This wait is the port's real per-frame point, so it is where the events
    // a game's TestEvent waits poll must be raised. Without it the classes in GameConfig are
    // configured but never delivered, and any such wait spins forever — which is exactly the stall
    // at func_8005CBB0 (it polls handle 0xF1000000, opened on class 0xF0000009).
    for (uint32_t cls : { c->cfg->irqEventClasses[0], c->cfg->irqEventClasses[1],
                          c->cfg->irqEventClasses[2] })
      if (cls) c->game->hle.deliverEvent(cls, 0xFFFFFFFFu);
    cur++;
    advanced++;
  }

  c->mem_w32(kVblankCounter, (uint32_t)cur);
  if (cfg_dbg("vsync"))
    cfg_logf("vsync", "wait target=%d -> counter=%d (+%d frames)", target, cur, advanced);
}

}  // namespace

void spyro_register_vsync(Game* g) {
  // Registration goes through PlatformHle::register_, which validates the address against
  // GameConfig::hle's windows — so this only installs if the libetc window actually covers it.
  g->platform_hle.register_(kVblankWait, vblank_wait);
}
