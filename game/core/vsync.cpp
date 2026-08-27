// Spyro 1's remaining libetc registration seam. Field service and cadence live in the title-owned
// FieldScheduler; the measured VSync entry itself is a mandatory fatal PlatformHle trap.
#include "core.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro1_field_scheduler.h"

#include <cstdint>
#include <lucent/log.h>

namespace {

constexpr std::uint32_t kVsyncCallback = 0x8005DE58u;

void observeVsyncCallback(Core *core) {
  spyro1::observeVblankCallback(*core, core->r[4]);
  gen_func_8005DE58(core);
}

} // namespace

void spyro_register_field_scheduler() {
  psxport_recomp()->shard_set_override(kVsyncCallback, observeVsyncCallback);
  lucent::info("fields",
               "native field scheduler registered; its host clock arms after boot. Guest VSync "
               "0x8005DBC4 is fail-fast and helper "
               "0x8005DD0C has no success override");
}
