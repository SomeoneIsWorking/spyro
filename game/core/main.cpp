#include "cfg.h"
#include "core.h"
#include "frame_loop_shell.h"
#include "game.h"
#include "host_turn.h"
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

void gte_init(void);
void load_exe(const char *path, Core *core);
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

  spyro::runtimeRun(core) = spyro::RuntimeRun(cfg_int("PSXPORT_NATIVE_FRAMES", 0));

  dc_boot_init(&core);

  std::uint32_t completedSteps = 0;
  while (!spyro::runtimeRun(core).shouldEnd()) {
    dc_step_frame(&core, ++completedSteps);
  }
  psx::cpu::shutdownHostTurn();
  spyro::reportRuntimeRun(core, completedSteps);
  return 0;
}
