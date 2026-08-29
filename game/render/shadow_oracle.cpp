#include "shadow_oracle.h"

#include "cfg.h"
#include "core.h"
#include "dualview_snapshot.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <lucent/log.h>

extern "C" void rec_dispatch(Core *core, std::uint32_t address);
extern "C" std::uint32_t gte_read_data(std::uint32_t reg);

namespace {

constexpr std::uint32_t kSpyroShadow = 0x80059A48u;
constexpr std::uint32_t kMobyShadow = 0x80059F8Cu;
constexpr std::uint32_t kPacketCursor = 0x800757B0u;
constexpr std::uint32_t kShadowOtBias = 0x8007AA2Cu;
constexpr std::uint32_t kPacketStride = 0x28u;
constexpr std::uint32_t kMaxPackets = 4096u;

struct State {
  std::uint32_t calls = 0;
  std::uint32_t compared = 0;
  std::uint32_t refused = 0;
  std::uint32_t limit = 0;
};

State s;

struct AnchorDepth {
  std::uint32_t sxy = 0;
  std::uint32_t sz = 0;
  bool seen = false;
};

bool guestSpan(std::uint32_t address, std::uint32_t bytes) {
  const std::uint32_t physical = address & 0x1fffffffu;
  return physical < 0x200000u && bytes <= 0x200000u - physical;
}

void restoreCpu(Core *core,
                const std::array<std::uint32_t, 32> &registers,
                std::uint32_t hi,
                std::uint32_t lo,
                std::uint32_t pc) {
  std::copy(registers.begin(), registers.end(), core->r);
  core->hi = hi;
  core->lo = lo;
  core->pc = pc;
}

void logPacket(
    Core *core, const char *name, std::uint32_t call, std::uint32_t packet, std::uint32_t base) {
  lucent::debug("shadoworacle",
                "call {} {} packet={} base=0x{:08X} tag={:08X} setup={:08X} zero={:08X} "
                "command={:08X} colour={:08X} anchor={:08X} point={:08X} point_next={:08X}",
                call,
                name,
                packet,
                base,
                core->mem_r32(base + 0x00u),
                core->mem_r32(base + 0x04u),
                core->mem_r32(base + 0x08u),
                core->mem_r32(base + 0x0cu),
                core->mem_r32(base + 0x10u),
                core->mem_r32(base + 0x14u),
                core->mem_r32(base + 0x1cu),
                core->mem_r32(base + 0x24u));
}

void captureAnchorPost(Core *core,
                       std::uint64_t ordinal,
                       std::uint32_t guestPc,
                       std::uint32_t instruction,
                       void *user) {
  (void)ordinal;
  (void)instruction;
  if (guestPc != 0x80059ADCu) {
    return;
  }
  auto *anchor = static_cast<AnchorDepth *>(user);
  anchor->sxy = gte_read_data(14u);
  anchor->sz = gte_read_data(19u) & 0xffffu;
  anchor->seen = true;
  (void)core;
}

void captureConsumer(Core *core, std::uint32_t address, const char *name) {
  const std::uint32_t before = core->mem_r32(kPacketCursor);
  if (!guestSpan(before, 0u)) {
    ++s.refused;
    lucent::error("shadoworacle",
                  "call {} {}: REFUSED invalid packet cursor 0x{:08X}; no body run",
                  s.calls,
                  name,
                  before);
    return;
  }

  std::array<std::uint32_t, 32> registers{};
  std::copy(std::begin(core->r), std::end(core->r), registers.begin());
  const std::uint32_t hi = core->hi;
  const std::uint32_t lo = core->lo;
  const std::uint32_t pc = core->pc;
  core->rsub.dualviewSnapshot.capturePre(core);

  AnchorDepth anchor;
  gte_op_observer_arm(core, nullptr, captureAnchorPost, &anchor);
  rec_dispatch(core, address);
  const std::uint64_t observedGteOps = gte_preop_observer_disarm(core);

  const std::uint32_t after = core->mem_r32(kPacketCursor);
  const bool monotonic = after >= before;
  const std::uint32_t bytes = monotonic ? after - before : 0u;
  const bool aligned = (bytes % kPacketStride) == 0u;
  const std::uint32_t packets = aligned ? bytes / kPacketStride : 0u;
  const bool bounded = packets <= kMaxPackets && guestSpan(before, bytes);
  if (!monotonic || !aligned || !bounded) {
    ++s.refused;
    lucent::error("shadoworacle",
                  "call {} {}: REFUSED packet range 0x{:08X}->0x{:08X} bytes={} aligned={} "
                  "bounded={}",
                  s.calls,
                  name,
                  before,
                  after,
                  bytes,
                  aligned ? 1 : 0,
                  bounded ? 1 : 0);
  } else {
    ++s.compared;
    lucent::info("shadoworacle",
                 "call {} {}: packet range 0x{:08X}->0x{:08X}, packets={} gate=0x{:08X} "
                 "shadow_cursor=0x{:08X}",
                 s.calls,
                 name,
                 before,
                 after,
                 packets,
                 core->mem_r32(0x8007aa34u),
                 core->mem_r32(0x80075f00u));
    for (std::uint32_t packet = 0; packet < packets; ++packet) {
      const std::uint32_t base = before + packet * kPacketStride;
      logPacket(core, name, s.calls, packet, base);
    }
    constexpr std::uint32_t kScratch = 0x1f800000u;
    std::array<std::uint32_t, 16> pointSxy{};
    std::array<std::uint32_t, 16> pointSz{};
    for (std::uint32_t point = 0; point < pointSxy.size(); ++point) {
      const std::uint32_t base = kScratch + point * 8u;
      pointSxy[point] = core->mem_r32(base);
      pointSz[point] = core->mem_r32(base + 4u) & 0xffffu;
    }
    lucent::debug("shadoworacle",
                  "call {} {} depth-anchor seen={} sxy={:08X} sz={:04X} gte_ops={} ot_bias={:04X}",
                  s.calls,
                  name,
                  anchor.seen ? 1 : 0,
                  anchor.sxy,
                  anchor.sz,
                  observedGteOps,
                  core->mem_r32(kShadowOtBias));
    for (std::uint32_t point = 0; point < pointSxy.size(); ++point) {
      lucent::debug("shadoworacle",
                    "call {} {} depth-point={} sxy={:08X} sz={:04X}",
                    s.calls,
                    name,
                    point,
                    pointSxy[point],
                    pointSz[point]);
    }
    const std::int32_t otBias = static_cast<std::int32_t>(core->mem_r32(kShadowOtBias));
    for (std::uint32_t packet = 0; packet < packets; ++packet) {
      const std::uint32_t base = before + packet * kPacketStride;
      const std::uint32_t pointSxyA = core->mem_r32(base + 0x1cu);
      const std::uint32_t pointSxyB = core->mem_r32(base + 0x24u);
      std::size_t pointA = pointSxy.size(), pointB = pointSxy.size();
      for (std::size_t point = 0; point < pointSxy.size(); ++point) {
        if (pointSxy[point] == pointSxyA && pointA == pointSxy.size()) {
          pointA = point;
        }
        if (pointSxy[point] == pointSxyB && pointB == pointSxy.size()) {
          pointB = point;
        }
      }
      if (!anchor.seen || pointA == pointSxy.size() || pointB == pointSxy.size()) {
        lucent::debug("shadoworacle",
                      "call {} {} depth-packet={} unresolved anchor={} point_a={} point_b={}",
                      s.calls,
                      name,
                      packet,
                      anchor.seen ? 1 : 0,
                      pointA == pointSxy.size() ? -1 : static_cast<int>(pointA),
                      pointB == pointSxy.size() ? -1 : static_cast<int>(pointB));
        continue;
      }
      const std::uint32_t pairSz = pointSz[pointA] + pointSz[pointB];
      const std::int32_t bucket =
          static_cast<std::int32_t>((pairSz + (pairSz >> 1u) + anchor.sz) >> 9u) - otBias;
      lucent::debug("shadoworacle",
                    "call {} {} depth-packet={} point_a={} sz_a={:04X} point_b={} sz_b={:04X} "
                    "anchor_sz={:04X} bucket={} admitted={}",
                    s.calls,
                    name,
                    packet,
                    pointA,
                    pointSz[pointA],
                    pointB,
                    pointSz[pointB],
                    anchor.sz,
                    bucket,
                    bucket >= 0 ? 1 : 0);
    }
  }

  core->rsub.dualviewSnapshot.restorePre(core);
  core->rsub.dualviewSnapshot.clearPre();
  restoreCpu(core, registers, hi, lo, pc);
}

} // namespace

void spyro_shadow_oracle_capture(Core *core) {
  if (core == nullptr || cfg_str("PSXPORT_SHADOW_ORACLE") == nullptr) {
    return;
  }
  if (s.calls == 0u) {
    s.limit = static_cast<std::uint32_t>(std::max(0, cfg_int("PSXPORT_SHADOW_ORACLE_CALLS", 1)));
    lucent::info("shadoworacle",
                 "ARMED for {} FIELD capture(s); retained bodies run from a snapshot and are "
                 "restored before native rendering continues",
                 s.limit);
  }
  if (s.calls >= s.limit) {
    return;
  }
  ++s.calls;
  captureConsumer(core, kMobyShadow, "moby");
  captureConsumer(core, kSpyroShadow, "spyro");
}
