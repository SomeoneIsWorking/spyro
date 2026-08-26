// Native ownership of libmcrd's event-stack push (0x80068F44).
//
// Ground truth is SCUS_942.28 0x80068F44..0x80068FC0 (32 instructions). All 13 static callers are
// in the executable's libmcrd region, and the overflow path prints "libmcrd: event overflow". Its
// only child is printf 0x8006279C, already owned in native_printf.cpp. The retained generated body
// remains the per-call differential oracle; issue 0027 records real reach on the title-screen
// memory-card path.
#include "memcard_event_stack.h"

#include "core.h"
#include "native_diff.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro_game.h"

namespace {

constexpr uint32_t kEventOverflowMessage = 0x800121C8u;

void pushMemcardEventNative(Core *c) {
  c->r[2] = 0x80070000u;
  c->r[2] = c->mem_r32(spyro::kMemcardEventStackIndex);
  c->r[29] -= 24u;
  c->r[6] = c->r[2] + 1u;
  const spyro::MemcardEventPushPlan plan = spyro::memcardEventPushPlan(c->r[6] - 1u);
  c->r[2] = static_cast<uint32_t>(plan.hasCapacity);
  c->mem_w32(c->r[29] + 16u, c->r[31]);

  if (!plan.hasCapacity) {
    c->r[4] = kEventOverflowMessage;
    c->r[31] = 0x80068F70u;
    func_8006279C(c);
  } else {
    c->r[5] = 3u;
    c->r[2] = c->r[6] << 4u;
    c->r[3] = plan.stateBase + spyro::kMemcardEventStateBytes - 4u;
    c->r[2] = c->r[6] << 2u;
    c->r[1] = 0x80070000u;
    c->mem_w32(spyro::kMemcardEventStackIndex, c->r[6]);
    c->r[1] = 0x80070000u;
    c->r[1] += c->r[2];
    c->mem_w32(plan.handlerSlot, c->r[4]);

    do {
      if (c->pending_work) {
        rec_irq_poll(c);
      }
      c->mem_w32(c->r[3], 0u);
      --c->r[5];
      const bool more = static_cast<int32_t>(c->r[5]) >= 0;
      c->r[3] -= 4u;
      if (!more) {
        break;
      }
    } while (true);
  }

  c->r[31] = c->mem_r32(c->r[29] + 16u);
  c->r[29] += 24u;
}

void pushMemcardEventOwned(Core *c) {
  ndiff_run(c, "memcard-event-push@0x80068F44", pushMemcardEventNative, gen_func_80068F44);
}

} // namespace

void spyro_register_native_memcard_event_stack() {
  psxport_recomp()->shard_set_override(0x80068F44u, pushMemcardEventOwned);
}
