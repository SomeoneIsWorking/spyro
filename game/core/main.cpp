#include "core.h"
#include "game.h"
#include "guest_execution.h"
#include "spyro_runtime.h"
#include "title_runtime_registry.h"
#include "title_selection.h"

#include <cstring>
#include <lucent/log.h>
#include <memory>

extern "C" {
void mdec_init(void);
void spu_init(void);
}

void gte_init(void);
void load_exe(const char *path, Core *core);

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

  load_exe(path, &core);
  gte_init();
  mdec_init();
  spu_init();
  game->spu_audio.init();
  game->gpu.gpu_native_init();
  runtime.registerOverrides(*game);

  const GuestProgramImage *program = runtime.guestProgramImage();
  if (!program || !program->crt0Entry) {
    lucent::error("executor", "{} has no authenticated runtime entry", selection.identity->serial);
    return 3;
  }
  spyro::GuestExecution execution(core, program->crt0Entry);
  psx::cpu::ExecutionResult result;
  do {
    result = execution.step(psx::cpu::ExecutionBudget::currentTurn(core));
  } while (result.reason == psx::cpu::ExecutionExitReason::BudgetExhausted);
  return spyro::reportExecutionResult(result, selection.identity->serial) ? 0 : 3;
}
