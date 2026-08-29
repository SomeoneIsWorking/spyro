#include "shadow_oracle.h"

#include "cfg.h"
#include "core.h"
#include "dualview_snapshot.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <lucent/log.h>

extern "C" void rec_dispatch(Core *core, std::uint32_t address);

namespace {

constexpr std::uint32_t kSpyroShadow = 0x80059A48u;
constexpr std::uint32_t kMobyShadow = 0x80059F8Cu;
constexpr std::uint32_t kPacketCursor = 0x800757B0u;
constexpr std::uint32_t kMaxPackets = 4096u;

struct State {
  std::uint32_t calls = 0;
  std::uint32_t compared = 0;
  std::uint32_t refused = 0;
  std::uint32_t limit = 0;
};

State s;

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

  rec_dispatch(core, address);

  const std::uint32_t after = core->mem_r32(kPacketCursor);
  const bool monotonic = after >= before;
  const std::uint32_t bytes = monotonic ? after - before : 0u;
  const bool aligned = (bytes % 0x28u) == 0u;
  const std::uint32_t packets = aligned ? bytes / 0x28u : 0u;
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
      const std::uint32_t base = before + packet * 0x28u;
      logPacket(core, name, s.calls, packet, base);
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
