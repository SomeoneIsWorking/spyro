#include "cfg.h"
#include "core.h"
#include "dbg_server.h" // debug_server_live — a client-driven run is uncapped
#include "frame_loop_shell.h"
#include "game.h"
#include "host_turn.h"
#include "hw_bind.h"
#include "psx_exe_image.h"
#include "runtime_run.h"
#include "spyro_game.h"
#include "spyro_runtime.h"
#include "title_runtime_registry.h"
#include "title_selection.h"

#include <cstring>
#include <lucent/log.h>
#include <memory>

extern "C" {
void watchdog_init(void);
void mdec_init(void);
void spu_init(void);
}

void dc_boot_init(Core *c);
void dc_step_frame(Core *c, uint32_t frame);

namespace {
constexpr const char *kDefaultExecutable = "scratch/assets/spyro1/SCUS_942.28";

bool helpRequested(int argc, char **argv) {
  return argc == 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0);
}

void printUsage(const char *program) {
  lucent::info("cli",
               "Usage: {} [executable]\nRun the serial-identified Spyro native/Lightrec port.",
               program);
}
} // namespace

int main(int argc, char **argv) {
  if (helpRequested(argc, argv)) {
    printUsage(argv[0]);
    return 0;
  }

  const char *path = argc > 1 ? argv[1] : kDefaultExecutable;
  const spyro::SelectionResult selection =
      spyro::selectExecutableFile(path, spyro::executableCatalog());
  if (!selection) {
    lucent::error("boot", "{}", selection.detail);
    return 2;
  }

  spyro::SpyroRuntime &runtime = spyro::runtimeFor(selection.identity->title);
  psxport_install_game(runtime);
  auto game = std::make_unique<Game>();
  Core &core = game->core;

  watchdog_init();
  load_exe(path, &core);
  gte_init();
  mdec_init();
  spu_init();
  game->spu_audio.init();
  game->gpu.gpu_native_init();

  // The live debug endpoint (PSXPORT_DEBUG_SERVER) is a framework service whose LIFETIME is not the
  // framework's boot spine's: this port owns its own frame driver and never enters native_boot's
  // loop, so the endpoint has to be attached here or it does not exist for this game at all.
  // Measured 2026-09-26: with the knob set, the product ran to the title screen and bound no port,
  // and the config audit named the knob UNKNOWN while reporting nothing else wrong.
  //
  // `attach` is the framework's one answer to both questions this spine has — start the endpoint,
  // and say what frame cap to run under, because a client-driven run must not be capped (the cap
  // exists to bound an unattended smoke run) while a cap of 0 means "run until told to stop". Then
  // `honourPause` before each frame and `service` after it, both the framework's. A title that
  // reimplements any of this is the second copy the factoring exists to prevent.
  const int frameCap = game->dbg_server.attach(&core, cfg_int("PSXPORT_NATIVE_FRAMES", 0));
  spyro::runtimeRun(core) = spyro::RuntimeRun(frameCap);

  dc_boot_init(&core);

  std::uint32_t completedSteps = 0;
  while (!spyro::runtimeRun(core).shouldEnd()) {
    game->dbg_server.honourPause(&core);
    dc_step_frame(&core, ++completedSteps);
    game->dbg_server.service(&core);
  }
  psx::cpu::shutdownHostTurn();
  spyro::reportRuntimeRun(core, completedSteps);
  return 0;
}
