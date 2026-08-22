#include "spyro_runtime.h"

#include "cfg.h"
#include "fntrace.h"
#include "hostprof.h"
#include "legacy_game_interface.h"
#include "spyro_context.h"
#include "spyro_game.h"

#include <lucent/log.h>

namespace spyro {

SpyroRuntime::SpyroRuntime()
    : LegacyGameRuntimeAdapter(legacy::measuredConfig(), legacy::compatibilityHooks()) {}

void *SpyroRuntime::createContext(Core &) {
  return new SpyroContext();
}

void SpyroRuntime::destroyContext(void *context) {
  delete static_cast<SpyroContext *>(context);
}

void SpyroRuntime::registerOverrides(Game &) {
  // Registration order is load-bearing because several diagnostics and native owners contend for
  // one override slot. fntrace stays last so collisions become visible instead of hiding the trace.
  hostprof_init();
  spyro_register_cd_queue();
  if (cfg_on("PSXPORT_NO_NATIVE")) {
    lucent::warn(
        "native",
        "PSXPORT_NO_NATIVE=1 — native bodies NOT installed; the substrate runs everything. "
        "Diagnostic only: any behaviour difference from a normal run is attributable to a "
        "natively-owned body.");
  } else {
    spyro_register_native_rand();
    spyro_register_native_leaves();
    spyro_register_native_vec();
    spyro_register_native_gte();
    spyro_register_native_angle();
    spyro_register_native_util();
    spyro_register_native_printf();
    spyro_register_native_actor_mesh_scratch();
    spyro_register_native_spu_pio_upload();
    spyro_register_native_spu_hardware_init();
    spyro_register_native_text_sprites();
    spyro_register_native_terrain();
    spyro_register_native_world();
    spyro_register_wide_clip();
    spyro_register_actor_chain_oracle();
    spyro_register_native_render();
  }
  fntrace_init();
}

void SpyroRuntime::bootInit(Core &core) {
  spyro_frame_loop_run(&core);
}

} // namespace spyro
