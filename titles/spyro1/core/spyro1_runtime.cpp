#include "spyro1_runtime.h"
#include "spyro1_frame_driver.h"

#include "cfg.h"
#include "field_environment_oracle.h"
#include "fntrace.h"
#include "fps60.h"
#include "game.h"
#include "hostprof.h"
#include "legacy_game_interface.h"
#include "overlay_table.h"
#include "presentation_owner.h"
#include "spyro_context.h"
#include "spyro_game.h"
#include "title_menu_oracle.h"

#include <lucent/log.h>

namespace spyro1 {

const GuestProgramImage Spyro1Runtime::programImage_{
    .bss = {0x80075640u, 0x8007AA38u},
    .stackTopWordAddress = 0x800755A8u,
    .stackReserveWordAddress = 0x800755A4u,
    .heapBase = 0x8007AA38u,
    .heapSizeStoreAddress = 0x800730C4u,
    .heapBaseStoreAddress = 0x800730C0u,
    .globalPointer = 0x80075264u,
    .libcInitEntry = 0x8005DB14u,
    .gameMainEntry = 0x80012204u,
    .crt0Entry = 0x8005B8E0u,
    .residentText = {REC_MAIN_LO, REC_MAIN_HI},
    .backtraceText = {},
    .stackBias = {true, -8},
};

Spyro1Runtime::Spyro1Runtime() : SpyroRuntime(programImage_, spyro::SpyroTitle::Spyro1) {
  bindLegacyInterface(&spyro::legacy::measuredConfig(), &spyro::legacy::compatibilityHooks());
}

bool Spyro1Runtime::installSubstrate() {
  spyro_install_recomp();
  return true;
}

std::string_view Spyro1Runtime::substrateRefusal() const {
  return {};
}

void *Spyro1Runtime::createContext(Core &) {
  return new SpyroContext();
}

void Spyro1Runtime::destroyContext(void *context) {
  delete static_cast<SpyroContext *>(context);
}

void Spyro1Runtime::registerOverrides(Game &) {
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
    spyro_register_native_memcard_event_stack();
    spyro_register_native_terrain();
    spyro_register_native_world();
    spyro_register_wide_clip();
    spyro_register_actor_chain_oracle();
    spyro_register_native_render();
    spyro_register_field_environment_oracle();
  }
  spyro_register_title_menu_oracle();
  fntrace_init();
}

void Spyro1Runtime::bootInit(Core &core) {
  frameDriver(core).initialize(core);
}

std::unique_ptr<FrameDriver> Spyro1Runtime::createFrameDriver(Game &game) {
  return std::make_unique<Spyro1FrameDriver>(game);
}

bool Spyro1Runtime::guestVramIsPicture(const Game &game) const {
  return spyro_presentation_owner(game.core).guestVramIsPicture();
}

std::unique_ptr<TemporalFramePresentation>
Spyro1Runtime::createTemporalFramePresentation(Game &game) {
  return std::make_unique<Fps60>(game);
}

} // namespace spyro1
