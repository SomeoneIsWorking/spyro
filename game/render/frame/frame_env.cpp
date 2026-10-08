// The frame the native producers draw into, ported from the guest's render driver 0x8001ED5C.
// Its DRAWENV layout is confirmed by the guest's own stage-13 handler, which writes
// kEnvA+0x19..1B and kEnvB+0x19..1B (0x80076EF9/FA/FB, 0x80076F7D/7E/7F) for the background colour.
#include "frame_env.h"
#include "core.h"
#include "gpu_native_internal.h"
#include "guest_actor_pool.h"
#include "guest_call.h"
#include "spyro1_field_scheduler.h"
#include <cstdlib>
#include <lucent/log.h>

namespace {

// The game's two draw environments; the per-parity pools live inside them.
constexpr uint32_t kEnvA = 0x80076EE0u;
constexpr uint32_t kEnvB = 0x80076F64u;
constexpr uint32_t kActiveEnvPtr = 0x80075888u; // *this = the env the guest is drawing with
constexpr uint32_t kDispEnvOfs = 0x5Cu;         // the DISPENV embedded at DRAWENV+0x5C
constexpr uint32_t kEnvPoolPtrOfs = 0x70u;
constexpr uint32_t kEnvOtPtrOfs = 0x74u;
constexpr uint32_t kEnvFrontPtrOfs = 0x78u;
constexpr uint32_t kPoolBase = 0x800757B0u;
constexpr uint32_t kPoolLimit = 0x80075780u;
constexpr uint32_t kOtBase = 0x80075820u;
constexpr uint32_t kFrontList = 0x8007581Cu;
constexpr uint32_t kPacketCount = 0x800758B0u;
constexpr uint32_t kTransientActorBytes = 0x1C000u;
constexpr uint32_t kBuildCameraMatrices = 0x80033C50u;

// Sony DRAWENV field offsets.
constexpr uint32_t kDeClipX = 0x00u, kDeClipY = 0x02u, kDeClipW = 0x04u, kDeClipH = 0x06u;
constexpr uint32_t kDeOfsX = 0x08u, kDeOfsY = 0x0Au;
constexpr uint32_t kDeTwX = 0x0Cu, kDeTwY = 0x0Eu, kDeTwW = 0x10u, kDeTwH = 0x12u;
constexpr uint32_t kDeTpage = 0x14u, kDeDtd = 0x16u, kDeDfe = 0x17u, kDeIsbg = 0x18u;
constexpr uint32_t kDeR0 = 0x19u, kDeG0 = 0x1Au, kDeB0 = 0x1Bu;
// DISPENV: RECT disp, RECT screen. Only disp differs between the two envs, and the guest's
// PutDispEnv caches the rest, so only the display start is reprogrammed.
constexpr uint32_t kDiDispX = 0x00u, kDiDispY = 0x02u;

// Vblank counter advanced by Spyro 1's FieldScheduler, and the two stamps the throttle compares.
constexpr uint32_t kVblankCounter = 0x800749E0u;
constexpr uint32_t kStampLastFrame = 0x80075950u; // the field count when the PREVIOUS frame ended
constexpr uint32_t kStampThisFrame = 0x80075954u; // …and this one's, written as the loop spins
constexpr int32_t kMinFieldsPerFrame =
    spyro::render::kFieldsPerLogicFrame; // the guest's own `< 2` test
constexpr int kMaxFieldsPerFrame = 8;    // this port's bound; the guest has none

} // namespace

std::uint32_t spyro::render::frameDisplayEnv(uint32_t drawEnv, bool fps60CommitPending) {
  if (drawEnv != kEnvA && drawEnv != kEnvB) {
    return 0;
  }
  if (!fps60CommitPending) {
    return drawEnv;
  }
  return drawEnv == kEnvA ? kEnvB : kEnvA;
}

// A typed lens over one of the game's DRAWENVs, so the GP0 words below read as what they are.
class DrawEnvLens {
public:
  DrawEnvLens(Core *c, uint32_t base) : mC(c), mB(base) {}
  int32_t clipX() const {
    return mC->mem_r16s(mB + kDeClipX);
  }
  int32_t clipY() const {
    return mC->mem_r16s(mB + kDeClipY);
  }
  int32_t clipW() const {
    return mC->mem_r16s(mB + kDeClipW);
  }
  int32_t clipH() const {
    return mC->mem_r16s(mB + kDeClipH);
  }
  int32_t ofsX() const {
    return mC->mem_r16s(mB + kDeOfsX);
  }
  int32_t ofsY() const {
    return mC->mem_r16s(mB + kDeOfsY);
  }
  int32_t twX() const {
    return mC->mem_r16s(mB + kDeTwX);
  }
  int32_t twY() const {
    return mC->mem_r16s(mB + kDeTwY);
  }
  int32_t twW() const {
    return mC->mem_r16s(mB + kDeTwW);
  }
  int32_t twH() const {
    return mC->mem_r16s(mB + kDeTwH);
  }
  uint32_t tpage() const {
    return mC->mem_r16(mB + kDeTpage);
  }
  bool dtd() const {
    return mC->mem_r8(mB + kDeDtd) != 0;
  }
  bool dfe() const {
    return mC->mem_r8(mB + kDeDfe) != 0;
  }
  bool isbg() const {
    return mC->mem_r8(mB + kDeIsbg) != 0;
  }
  uint32_t bgR() const {
    return mC->mem_r8(mB + kDeR0);
  }
  uint32_t bgG() const {
    return mC->mem_r8(mB + kDeG0);
  }
  uint32_t bgB() const {
    return mC->mem_r8(mB + kDeB0);
  }
  int32_t dispX() const {
    return mC->mem_r16s(mB + kDispEnvOfs + kDiDispX);
  }
  int32_t dispY() const {
    return mC->mem_r16s(mB + kDispEnvOfs + kDiDispY);
  }
  uint32_t base() const {
    return mB;
  }

private:
  Core *mC;
  uint32_t mB;
};

// Opens the native leg's frame: establishes the env's arenas, then programs the GPU. Returns the
// env now being drawn with, so the caller can hand the same one to spyro::render::frameEnd.
std::uint32_t spyro::render::frameBegin(Core *c) {
  // The driver head of 0x8001ED5C, through its camera-matrix call at 0x80033C50.
  const uint32_t env = (c->mem_r32(kActiveEnvPtr) == kEnvA) ? kEnvB : kEnvA;
  const uint32_t pool = c->mem_r32(env + kEnvPoolPtrOfs);
  // actor_cursor..actor_end is also the transient actor arena 0x80022A2C consumes.
  const uint32_t actor_end = pool + kTransientActorBytes;
  c->mem_w32(kOtBase, c->mem_r32(env + kEnvOtPtrOfs));
  c->mem_w32(kFrontList, c->mem_r32(env + kEnvFrontPtrOfs));
  c->mem_w32(kPoolBase, pool);
  c->mem_w32(kPacketCount, 0);
  c->mem_w32(spyro::guest_actor_pool::kEndAddress, actor_end);
  c->mem_w32(spyro::guest_actor_pool::kCursorAddress, actor_end);
  c->mem_w32(kPoolLimit, actor_end);
  c->mem_w32(kActiveEnvPtr, env);
  psx::cpu::dispatchGuestToReturn0(*c,
                                   kBuildCameraMatrices,
                                   psx::cpu::ExecutionBudget::currentTurn(*c),
                                   "build-camera-matrices");

  const DrawEnvLens de(c, env);
  // SetDrawEnv's five GP0 words, in its order.
  gpu_gp0(c, 0xE3000000u | (uint32_t)((de.clipY() & 0x3FF) << 10) | (uint32_t)(de.clipX() & 0x3FF));
  gpu_gp0(c,
          0xE4000000u | (uint32_t)(((de.clipY() + de.clipH() - 1) & 0x3FF) << 10) |
              (uint32_t)((de.clipX() + de.clipW() - 1) & 0x3FF));
  gpu_gp0(c, 0xE5000000u | (uint32_t)((de.ofsY() & 0x7FF) << 11) | (uint32_t)(de.ofsX() & 0x7FF));
  gpu_gp0(
      c, 0xE1000000u | (de.tpage() & 0x1FFu) | (de.dtd() ? 0x200u : 0u) | (de.dfe() ? 0x400u : 0u));
  // The texture window is a mask/offset in 8-texel units; libgpu packs it exactly this way.
  gpu_gp0(c,
          0xE2000000u | (uint32_t)(((de.twW() >> 3) & 0x1F)) |
              (uint32_t)(((de.twH() >> 3) & 0x1F) << 5) |
              (uint32_t)(((de.twX() >> 3) & 0x1F) << 10) |
              (uint32_t)(((de.twY() >> 3) & 0x1F) << 15));
  gpu_gp0(c, 0xE6000000u);
  // The background fill the same builder appends when isbg is set. Both envs are 64-aligned, so
  // libgpu takes the GP0(0x02) FILL arm; the GP0(0x60) rectangle arm is not ported and warns.
  if (de.isbg()) {
    if ((de.clipX() & 0x3F) == 0 && (de.clipW() & 0x3F) == 0) {
      gpu_gp0(c, 0x02000000u | (de.bgB() << 16) | (de.bgG() << 8) | de.bgR());
      gpu_gp0(c, (uint32_t)((de.clipY() & 0xFFFF) << 16) | (uint32_t)(de.clipX() & 0xFFFF));
      gpu_gp0(c, (uint32_t)((de.clipH() & 0xFFFF) << 16) | (uint32_t)(de.clipW() & 0xFFFF));
    } else {
      lucent::warn("frameenv",
                   "drawenv 0x{:08X} clip=({},{},{},{}) is NOT 64-aligned — libgpu would "
                   "emit the GP0(0x60) rectangle form, which this port has not RE'd. "
                   "NO BACKGROUND WAS FILLED this frame.",
                   env,
                   de.clipX(),
                   de.clipY(),
                   de.clipW(),
                   de.clipH());
    }
  }
  lucent::debug("frameenv",
                "begin env=0x{:08X} clip=({},{},{},{}) ofs=({},{}) tpage={:04X} isbg={} "
                "bg=({},{},{})",
                env,
                de.clipX(),
                de.clipY(),
                de.clipW(),
                de.clipH(),
                de.ofsX(),
                de.ofsY(),
                de.tpage(),
                de.isbg() ? 1 : 0,
                de.bgR(),
                de.bgG(),
                de.bgB());
  return env;
}

// Closes the frame: spends the guest's display fields, then shows the buffer this env names. The
// retail tail draws no more than every second field, and Spyro pairs each DRAWENV with the OTHER
// buffer's DISPENV (PutDispEnv(activeEnv + 0x5C)), so this shows the previous iteration's frame.
void spyro::render::frameEnd(Core *c, uint32_t env, bool fps60CommitPending) {
  // The >= 2-field throttle, on the game's own stamps.
  spyro1::deliverNativeField(*c, "nativeframe", fps60CommitPending);
  int32_t now = (int32_t)c->mem_r32(kVblankCounter);
  c->mem_w32(kStampThisFrame, (uint32_t)now);
  int fields = 1;
  while (now - (int32_t)c->mem_r32(kStampLastFrame) < kMinFieldsPerFrame) {
    if (fields >= kMaxFieldsPerFrame) {
      // The guest's loop has no bound; an unbounded wait here would hang the process on a stale
      // stamp instead of naming it.
      lucent::error("frameenv",
                    "field throttle stopped after {} fields: counter={} stamp={} — the "
                    "vblank counter or the stamp at 0x{:08X} is not advancing.",
                    fields,
                    now,
                    (int32_t)c->mem_r32(kStampLastFrame),
                    kStampLastFrame);
      std::abort();
    }
    spyro1::deliverNativeField(*c, "nativeframe", fps60CommitPending);
    now = (int32_t)c->mem_r32(kVblankCounter);
    c->mem_w32(kStampThisFrame, (uint32_t)now);
    fields++;
  }
  c->mem_w32(kStampLastFrame, (uint32_t)now);

  const uint32_t selectedEnv = spyro::render::frameDisplayEnv(env, fps60CommitPending);
  if (!selectedEnv) {
    lucent::error("frameenv", "FATAL: unknown draw env 0x{:08X}", env);
    abort();
  }
  const DrawEnvLens draw(c, env), selected(c, selectedEnv);
  gpu_gp1(c,
          0x05000000u | (uint32_t)((selected.dispY() & 0x3FF) << 10) |
              (uint32_t)(selected.dispX() & 0x3FF));
  static uint64_t selectedFrames = 0, wrongHalf = 0;
  ++selectedFrames;
  const bool wrong =
      fps60CommitPending && (selected.dispX() != draw.ofsX() || selected.dispY() != draw.ofsY());
  wrongHalf += wrong;
  lucent::debug("frameenv",
                "end env=0x{:08X} draw clip=({},{},{},{}) ofs=({},{}) guest_disp=({},{}) "
                "selected_env=0x{:08X} selected_disp=({},{}) fps60={} fields={} "
                "wrong_half={}/{}",
                env,
                draw.clipX(),
                draw.clipY(),
                draw.clipW(),
                draw.clipH(),
                draw.ofsX(),
                draw.ofsY(),
                draw.dispX(),
                draw.dispY(),
                selectedEnv,
                selected.dispX(),
                selected.dispY(),
                fps60CommitPending ? 1 : 0,
                fields,
                wrongHalf,
                selectedFrames);
}
