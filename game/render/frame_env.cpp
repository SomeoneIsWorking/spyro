// frame_env.cpp — THE FRAME THE NATIVE PRODUCERS DRAW INTO.
//
// WHY THIS EXISTS. A producer that emits perfect quads into a buffer nobody is looking at draws
// nothing, and that failure is indistinguishable from a broken producer. On the reference leg the
// guest's render driver 0x8001ED5C opens the frame (it flips the draw env) and closes it
// (PutDispEnv/PutDrawEnv/DrawOTag); on the NATIVE leg that driver never runs, so without this file
// the draw offset, the draw-area clip and the display origin are all frozen at whatever the last
// reference frame left — which is, by construction, the buffer that is NOT on screen.
//
// So this is not "setup". It is re-frontier step `frame.own-render-driver` parts (1) and (2), owned
// game-side because the values it programs are the GAME'S OWN DRAWENV/DISPENV structs.
//
// ────────────────────────────────────────────────────────────────────────────────────────────────
// RE — GROUND TRUTH. Ghidra headless over a RAM dump taken at the title screen
// (scratch/raw/snap_title_3000.bin, resident overlay OV_5B800 confirmed by tools/whatis.py):
//   scratch/decomp/frameloop.c        FUN_8001ed5c  — the driver head: the env flip
//   scratch/decomp/libgpu_setdrawenv.c FUN_800608e0 — SetDrawEnv: which GP0 words a DRAWENV becomes
//   scratch/decomp/libgpu_env.c       FUN_80060030 — PutDispEnv: which GP1 word a DISPENV becomes
//
// 1. THE FRAME-OWNED ARENAS, verbatim from 0x8001ED5C's head:
//        env = (active == kEnvA) ? kEnvB : kEnvA;
//        ot = env->ot; front = env->front; pool = env->pool; packet_count = 0;
//        actor_end = pool + 0x1c000; actor_cursor = actor_limit = actor_end; active = env;
//        0x80033C50();
//    These are not merely guest-packet bookkeeping. 0x80018880 and the stage-13 text builder use
//    actor_cursor..actor_end as the transient actor arena before 0x80022A2C consumes that queue.
//    Omitting the assignments made the native handler subtract an actor from null and write at
//    0xFEFFFFFE. The final call builds the two camera matrices at 0x80076DD0/0x80076DE4; it remains
//    guest logic, dispatched before any native producer reads them.
//
// 2. THE DRAW ENV -> GP0, from FUN_800608e0's five packet words, in its order:
//        E3 draw-area TL   = (clip.x,           clip.y)
//        E4 draw-area BR   = (clip.x+clip.w-1,  clip.y+clip.h-1)
//        E5 draw offset    = (ofs[0],           ofs[1])
//        E1 tpage          = env.tpage | dtd<<9 | dfe<<10
//        E2 texture window = env.tw
//    plus, when `isbg` is set, the background FILL the same builder appends:
//        GP0(0x02) colour = (r0,g0,b0), rect = clip  (the 64-aligned form; see below)
//
// 3. THE DISPLAY ENV -> GP1, from FUN_80060030's non-interlace arm:
//        GP1(0x05) display start = (disp.y & 0x3FF) << 10 | (disp.x & 0x3FF)
//    The rest of PutDispEnv (GP1 06/07 ranges, GP1 08 mode) is guarded in the guest by a cache of
//    the last values (DAT_80074AD0..) and does not change between Spyro's two envs — measured: the
//    two DISPENVs differ only in `disp`, and both `screen` rects are (0,0,0,0). So only the display
//    START is reprogrammed here, and this file says so rather than implying it ported all of libgpu.
//
// THE STRUCT LAYOUT IS NOT ASSUMED — IT IS CONFIRMED. Sony's DRAWENV puts (r0,g0,b0) at +0x19/1A/1B,
// and the guest's own stage-13 handler writes exactly 0x80076EF9/EFA/EFB and 0x80076F7D/7E/7F
// (scratch/decomp/title_stage13.c). Those are kEnvA+0x19.. and kEnvB+0x19.., which pins both env
// bases AND the field layout from the game's own code.
#include "frame_env.h"
#include "core.h"
#include <lucent/log.h>

// `core.h` declares `void gpu_gp1(uint32_t)` — a declaration with NO definition anywhere in the
// framework (the real symbol is `gpu_gp1(Core*, uint32_t)`, gpu_native.cpp:2182). Calling the
// declared-but-absent overload is a link error, so the correct one is declared here. That stale
// declaration is a framework wart worth removing upstream; it is not this port's to fix.
void gpu_gp1(Core* core, uint32_t w);

namespace {

// ── The game's two draw environments (claim C073: per-parity pools live INSIDE the envs) ─────────
// Confirmed by the background-colour stores in 0x8007CEE4 (see the header comment above).
constexpr uint32_t kEnvA        = 0x80076EE0u;
constexpr uint32_t kEnvB        = 0x80076F64u;
constexpr uint32_t kActiveEnvPtr = 0x80075888u;   // *this = the env the guest is drawing with
constexpr uint32_t kDispEnvOfs  = 0x5Cu;          // the DISPENV embedded at DRAWENV+0x5C
constexpr uint32_t kEnvPoolPtrOfs = 0x70u;
constexpr uint32_t kEnvOtPtrOfs = 0x74u;
constexpr uint32_t kEnvFrontPtrOfs = 0x78u;
constexpr uint32_t kPoolBase = 0x800757B0u;
constexpr uint32_t kPoolEnd = 0x800756FCu;
constexpr uint32_t kPoolCursor = 0x80075710u;
constexpr uint32_t kPoolLimit = 0x80075780u;
constexpr uint32_t kOtBase = 0x80075820u;
constexpr uint32_t kFrontList = 0x8007581Cu;
constexpr uint32_t kPacketCount = 0x800758B0u;
constexpr uint32_t kTransientActorBytes = 0x1C000u;
constexpr uint32_t kBuildCameraMatrices = 0x80033C50u;

// Sony DRAWENV field offsets. Every one is used below, so an unused constant cannot rot here.
constexpr uint32_t kDeClipX = 0x00u, kDeClipY = 0x02u, kDeClipW = 0x04u, kDeClipH = 0x06u;
constexpr uint32_t kDeOfsX  = 0x08u, kDeOfsY  = 0x0Au;
constexpr uint32_t kDeTwX   = 0x0Cu, kDeTwY   = 0x0Eu, kDeTwW = 0x10u, kDeTwH = 0x12u;
constexpr uint32_t kDeTpage = 0x14u, kDeDtd = 0x16u, kDeDfe = 0x17u, kDeIsbg = 0x18u;
constexpr uint32_t kDeR0 = 0x19u, kDeG0 = 0x1Au, kDeB0 = 0x1Bu;
// DISPENV: RECT disp, RECT screen. Only `disp` is read — see note 3 above.
constexpr uint32_t kDiDispX = 0x00u, kDiDispY = 0x02u;

// The display tail's own state: the guest's vblank counter (the same address game/core/vsync.cpp
// serves VSync from) and the two field stamps its >= 2-field throttle compares.
constexpr uint32_t kVblankCounter   = 0x800749E0u;
constexpr uint32_t kStampLastFrame  = 0x80075950u;   // the field count when the PREVIOUS frame ended
constexpr uint32_t kStampThisFrame  = 0x80075954u;   // …and this one's, written as the loop spins
constexpr int32_t  kMinFieldsPerFrame = 2;           // the guest's own `< 2` test
constexpr int      kMaxFieldsPerFrame = 8;           // this port's bound; the guest has none

}  // namespace

// game/core/vsync.cpp — the port's ONE definition of "a display field happened": flush, pad, the
// guest's vblank callback, present. Declared here rather than in a header because vsync.cpp is the
// only other file that uses it, and a two-caller extern is clearer than a header nobody else reads.
bool spyro_deliver_field(Core* c, const char* site);
extern "C" void rec_dispatch(Core* c, uint32_t addr);

// A typed lens over one of the game's DRAWENVs, so the GP0 words below read as what they are.
class DrawEnvLens {
public:
  DrawEnvLens(Core* c, uint32_t base) : mC(c), mB(base) {}
  int32_t clipX() const { return mC->mem_r16s(mB + kDeClipX); }
  int32_t clipY() const { return mC->mem_r16s(mB + kDeClipY); }
  int32_t clipW() const { return mC->mem_r16s(mB + kDeClipW); }
  int32_t clipH() const { return mC->mem_r16s(mB + kDeClipH); }
  int32_t ofsX()  const { return mC->mem_r16s(mB + kDeOfsX); }
  int32_t ofsY()  const { return mC->mem_r16s(mB + kDeOfsY); }
  int32_t twX()   const { return mC->mem_r16s(mB + kDeTwX); }
  int32_t twY()   const { return mC->mem_r16s(mB + kDeTwY); }
  int32_t twW()   const { return mC->mem_r16s(mB + kDeTwW); }
  int32_t twH()   const { return mC->mem_r16s(mB + kDeTwH); }
  uint32_t tpage() const { return mC->mem_r16(mB + kDeTpage); }
  bool    dtd()   const { return mC->mem_r8(mB + kDeDtd) != 0; }
  bool    dfe()   const { return mC->mem_r8(mB + kDeDfe) != 0; }
  bool    isbg()  const { return mC->mem_r8(mB + kDeIsbg) != 0; }
  uint32_t bgR()  const { return mC->mem_r8(mB + kDeR0); }
  uint32_t bgG()  const { return mC->mem_r8(mB + kDeG0); }
  uint32_t bgB()  const { return mC->mem_r8(mB + kDeB0); }
  int32_t dispX() const { return mC->mem_r16s(mB + kDispEnvOfs + kDiDispX); }
  int32_t dispY() const { return mC->mem_r16s(mB + kDispEnvOfs + kDiDispY); }
  uint32_t base() const { return mB; }

private:
  Core* mC;
  uint32_t mB;
};

// nativeFrameBegin — open the native leg's frame: establish its arenas, then program the GPU.
// Returns the env now being drawn with, so the caller can hand the same one to nativeFrameEnd.
uint32_t nativeFrameBegin(Core* c) {
  // (1) THE COMPLETE DRIVER HEAD — 0x8001ED5C, through its camera-matrix call (see note 1).
  const uint32_t env = (c->mem_r32(kActiveEnvPtr) == kEnvA) ? kEnvB : kEnvA;
  const uint32_t pool = c->mem_r32(env + kEnvPoolPtrOfs);
  const uint32_t actor_end = pool + kTransientActorBytes;
  c->mem_w32(kOtBase, c->mem_r32(env + kEnvOtPtrOfs));
  c->mem_w32(kFrontList, c->mem_r32(env + kEnvFrontPtrOfs));
  c->mem_w32(kPoolBase, pool);
  c->mem_w32(kPacketCount, 0);
  c->mem_w32(kPoolEnd, actor_end);
  c->mem_w32(kPoolCursor, actor_end);
  c->mem_w32(kPoolLimit, actor_end);
  c->mem_w32(kActiveEnvPtr, env);
  rec_dispatch(c, kBuildCameraMatrices);

  const DrawEnvLens de(c, env);
  // (2) THE DRAW ENV — FUN_800608e0's words, in its order.
  gpu_gp0(c, 0xE3000000u | (uint32_t)((de.clipY() & 0x3FF) << 10) | (uint32_t)(de.clipX() & 0x3FF));
  gpu_gp0(c, 0xE4000000u | (uint32_t)(((de.clipY() + de.clipH() - 1) & 0x3FF) << 10)
                         | (uint32_t)((de.clipX() + de.clipW() - 1) & 0x3FF));
  gpu_gp0(c, 0xE5000000u | (uint32_t)((de.ofsY() & 0x7FF) << 11) | (uint32_t)(de.ofsX() & 0x7FF));
  gpu_gp0(c, 0xE1000000u | (de.tpage() & 0x1FFu) | (de.dtd() ? 0x200u : 0u) | (de.dfe() ? 0x400u : 0u));
  // The texture window is a mask/offset in 8-texel units; libgpu packs it exactly this way.
  gpu_gp0(c, 0xE2000000u | (uint32_t)(((de.twW() >> 3) & 0x1F))
                         | (uint32_t)(((de.twH() >> 3) & 0x1F) << 5)
                         | (uint32_t)(((de.twX() >> 3) & 0x1F) << 10)
                         | (uint32_t)(((de.twY() >> 3) & 0x1F) << 15));
  gpu_gp0(c, 0xE6000000u);
  // The BACKGROUND FILL the same builder appends when isbg is set. Spyro's clip is 64-aligned and
  // 512 wide on both envs (measured: (0,8,512,224) and (0,248,512,224)), i.e. exactly the arm
  // FUN_800608e0 takes when `(clip.x & 0x3f) == 0 && (clip.w & 0x3f) == 0` — the GP0(0x02) FILL.
  // The other arm (a GP0(0x60) rectangle, used when the clip is not 64-aligned) is NOT ported: it
  // cannot be reached by this game's envs, and a branch that no input can take is a branch nobody
  // can verify. If a future env breaks that alignment this warns instead of drawing the wrong thing.
  if (de.isbg()) {
    if ((de.clipX() & 0x3F) == 0 && (de.clipW() & 0x3F) == 0) {
      gpu_gp0(c, 0x02000000u | (de.bgB() << 16) | (de.bgG() << 8) | de.bgR());
      gpu_gp0(c, (uint32_t)((de.clipY() & 0xFFFF) << 16) | (uint32_t)(de.clipX() & 0xFFFF));
      gpu_gp0(c, (uint32_t)((de.clipH() & 0xFFFF) << 16) | (uint32_t)(de.clipW() & 0xFFFF));
    } else {
      lucent::warn("frameenv", "drawenv 0x{:08X} clip=({},{},{},{}) is NOT 64-aligned — libgpu would "
                               "emit the GP0(0x60) rectangle form, which this port has not RE'd. "
                               "NO BACKGROUND WAS FILLED this frame.",
                   env, de.clipX(), de.clipY(), de.clipW(), de.clipH());
    }
  }
  lucent::debug("frameenv", "begin env=0x{:08X} clip=({},{},{},{}) ofs=({},{}) tpage={:04X} isbg={} "
                            "bg=({},{},{})",
                env, de.clipX(), de.clipY(), de.clipW(), de.clipH(), de.ofsX(), de.ofsY(),
                de.tpage(), de.isbg() ? 1 : 0, de.bgR(), de.bgG(), de.bgB());
  return env;
}

// nativeFrameEnd — close the frame: SPEND THE FRAME'S FIELDS, then show the buffer this env names.
//
// THE FIELD WAIT IS NOT OPTIONAL, and leaving it out is how this file earned its second half.
// MEASURED before it existed: the native leg ran 1556 drawn frames and the port PRESENTED NOTHING
// during any of them (`PSXPORT_DEBUG=rqflush,presentskip`: a continuous run of drawFrame flushes with
// not one presentskip line between them). Spyro's ONLY per-frame vblank wait lives in this tail, the
// port's present/pad/vblank-callback all hang off that wait (game/core/vsync.cpp `deliver_field`), and
// the native leg was skipping the tail — so the composite stayed frozen on the boot logo and the
// producer's quads, correctly built and correctly queued, were never composited. A producer nobody
// can see is indistinguishable from a broken one; that is why this is here and not deferred.
//
// RE — the tail of FUN_8007cee4 / FUN_8001ed5c, verbatim (scratch/decomp/title_stage13.c):
//     DrawSync(0);                                   // 0x8005F764
//     VSync(0);                                      // 0x8005DBC4
//     [0x80075954] = VSync(-1);
//     while ([0x80075954] - [0x80075950] < 2) { VSync(0); [0x80075954] = VSync(-1); }
//     [0x80075950] = VSync(-1);
// i.e. spend at least one field, then keep spending until at least TWO have passed since the stamp
// the previous frame left — Spyro's 30 Hz logic rate on a 60 Hz display. Both stamps are the GAME'S
// OWN globals and are written here exactly as the guest writes them, so the reference leg picks the
// timebase up unchanged if a run switches legs.
//
// DrawSync(0) has no native counterpart: it waits for the guest's GPU DMA to drain, and the native
// leg issues no DMA. Named rather than silently dropped.
//
// `deliver_field` IS the port's one definition of "a field happened" (it flushes the queue, services
// the pad, runs the guest's vblank callback and presents). Calling it — rather than counting fields
// here — is what keeps the native and reference legs on ONE timebase instead of two that drift.
//
// NOTE THE ONE-FRAME OFFSET, because it looks like a bug and is not. Spyro pairs each DRAWENV with
// the DISPENV of the OTHER buffer (measured: env 0x80076EE0 draws at ofs y=0 and displays y=240; env
// 0x80076F64 draws at y=240 and displays y=0). So "display what this env says" shows the frame drawn
// on the PREVIOUS iteration, which is the whole point of a double buffer. The guest's own tail does
// exactly this — PutDispEnv(activeEnv + 0x5C) — and porting it any other way would be inventing a
// different frame policy while claiming to reproduce this one.
void nativeFrameEnd(Core* c, uint32_t env) {
  // The >= 2-field throttle, on the game's own stamps.
  spyro_deliver_field(c, "nativeframe");
  int32_t now = (int32_t)c->mem_r32(kVblankCounter);
  c->mem_w32(kStampThisFrame, (uint32_t)now);
  int fields = 1;
  while (now - (int32_t)c->mem_r32(kStampLastFrame) < kMinFieldsPerFrame) {
    if (fields >= kMaxFieldsPerFrame) {
      // The guest's loop has no bound; this port's does, because an unbounded wait here would hang
      // the whole process on a stale stamp instead of saying which stamp went wrong.
      lucent::warn("frameenv", "field throttle gave up after {} fields: counter={} stamp={} — the "
                               "vblank counter or the stamp at 0x{:08X} is not advancing.",
                   fields, now, (int32_t)c->mem_r32(kStampLastFrame), kStampLastFrame);
      break;
    }
    spyro_deliver_field(c, "nativeframe");
    now = (int32_t)c->mem_r32(kVblankCounter);
    c->mem_w32(kStampThisFrame, (uint32_t)now);
    fields++;
  }
  c->mem_w32(kStampLastFrame, (uint32_t)now);

  const DrawEnvLens de(c, env);
  gpu_gp1(c, 0x05000000u | (uint32_t)((de.dispY() & 0x3FF) << 10) | (uint32_t)(de.dispX() & 0x3FF));
  lucent::debug("frameenv", "end   env=0x{:08X} display start=({},{}) fields={}",
                env, de.dispX(), de.dispY(), fields);
}
