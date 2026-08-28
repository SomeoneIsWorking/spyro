#include "cfg.h"
#include "core.h"
#include "game.h"
#include "spyro2_recomp_register.h"
#include "spyro2_runtime.h"
#include "spyro_title_catalog.generated.h"
#include "title_selection.h"

#include <cstdio>
#include <cstring>
#include <lucent/log.h>
#include <memory>

extern "C" {
void watchdog_init(void);
void mdec_init(void);
void spu_init(void);
}

void load_exe(const char *path, Core *core);
void dc_boot_init(Core *core);
void dc_step_frame(Core *core, std::uint32_t frame);

namespace {

constexpr const char *kDefaultExecutable = "scratch/bin/spyro2/SCUS_944.25";

bool helpRequested(int argc, char **argv) {
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "-h") == 0 || std::strcmp(argv[index], "--help") == 0) {
      return true;
    }
  }
  return false;
}

void printUsage(const char *program) {
  std::printf("Usage: %s [SCUS_944.25]\n", program);
  std::printf("Run the measured Spyro 2 native-owned crt0/boot product.\n\n");
  std::printf("Options:\n  -h, --help  show this help and exit\n");
}

} // namespace

int main(int argc, char **argv) {
  if (helpRequested(argc, argv)) {
    printUsage(argv[0]);
    return 0;
  }

  const char *path = argc > 1 ? argv[1] : kDefaultExecutable;
  const spyro::SelectionResult selection =
      spyro::selectExecutableFile(path, spyro::generated::kExecutableCatalog);
  if (!selection) {
    lucent::error("spyro2-boot", "{}", selection.detail);
    return 2;
  }
  if (selection.identity->title != spyro::SpyroTitle::Spyro2) {
    lucent::error("spyro2-boot", "selected executable is not SCUS_944.25");
    return 2;
  }

  spyro2::Spyro2Runtime runtime(spyro2::installRecompSubstrate);
  psxport_install_game(runtime);
  if (!runtime.installSubstrate()) {
    lucent::error("spyro2-boot", "{}", runtime.substrateRefusal());
    return 2;
  }

  auto game = std::make_unique<Game>();
  Core &core = game->core;
  game->gpu_vk.tritest();
  watchdog_init();
  load_exe(path, &core);

  void gte_init(void);
  void threads_init(Core *);
  void threads_register_overrides(void);

  gte_init();
  mdec_init();
  spu_init();
  game->spu_audio.init();
  game->gpu.gpu_native_init();
  threads_init(&core);
  threads_register_overrides();

  core.r[4] = 1;
  core.r[5] = 0;
  dc_boot_init(&core);
  for (std::uint32_t frame = 1;; ++frame) {
    dc_step_frame(&core, frame);
  }
}
