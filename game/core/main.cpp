// game/core/main.cpp — the Spyro port's process entry point.
//
// main() is game-side: it installs the derived runtime and generated substrate, brings up the
// framework's PSX hardware backends, loads the executable, and boots it. After the installs
// it touches only framework symbols.
#include "cfg.h" // cfg_str — PSXPORT_SELFTEST is a feature flag, not a diagnostic
#include "core.h"
#include "game.h"
#include "platform_hle.h"
#include "producer_run.h" // the graphics-producer DB's lifecycle — this port owns it (issue #58)
#include "spyro_game.h"
#include "title_runtime_registry.h"
#include "title_selection.h"
#include <lucent/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
void watchdog_init(void);
void mdec_init(void);
void spu_init(void);
}

void load_exe(const char *path, Core *c); // framework: runtime/recomp/boot.cpp
void dc_boot_init(Core *c); // framework: crt0_setup + finite title boot initialization
void dc_step_frame(Core *c, uint32_t frame); // framework shell -> one title FrameDriver step
int selftest_run(const char *path);          // framework: runtime/recomp/selftest.cpp

// Spyro 1 boots straight from SCUS_942.28 — unlike Tomba!2 there is no separate SCEA boot stub that
// LoadExec's a MAIN.EXE, so there is no stub stage to run and no second image to load. The
// executable basename is also the serial-selection input; the bytes must then match that title's
// exact identity.
static const char *kDefaultExe = "scratch/bin/spyro/SCUS_942.28";

static bool helpRequested(int argc, char **argv) {
  for (int index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "-h") == 0 || strcmp(argv[index], "--help") == 0) {
      return true;
    }
  }
  return false;
}

static void printUsage(const char *program) {
  printf("Usage: %s [executable]\n", program);
  printf("Run the serial-identified Spyro native PC port.\n\n");
  printf("Arguments:\n");
  printf("  executable  extracted PSX executable (default: %s)\n\n", kDefaultExe);
  printf("Options:\n");
  printf("  -h, --help  show this help and exit\n");
}

int main(int argc, char **argv) {
  // Help is a process interface, not a boot mode. Handle it before executable identity selection,
  // runtime installation, GPU setup, or disc/asset discovery so it works in a bare checkout.
  if (helpRequested(argc, argv)) {
    printUsage(argv[0]);
    return 0;
  }

  const char *path = argc > 1 ? argv[1] : kDefaultExe;
  const spyro::SelectionResult selection =
      spyro::selectExecutableFile(path, spyro::executableCatalog());
  if (!selection) {
    lucent::error("boot", "{}", selection.detail);
    return 2;
  }

  // Core snapshots the installed runtime during construction. Identity selection, runtime install,
  // and substrate refusal therefore all happen before Game: a Spyro 2/3 executable can never
  // inherit Spyro 1's generated code, GameConfig compatibility views, or native owners.
  spyro::SpyroRuntime &runtime = spyro::runtimeFor(selection.identity->title);
  psxport_install_game(runtime);
  if (!runtime.installSubstrate()) {
    lucent::error("boot", "{}", runtime.substrateRefusal());
    return 2;
  }
  lucent::info("boot", "{}", selection.detail);

  Game *game = new Game();
  Core *c = &game->core;

  // Framework GPU self-tests run before the executable/disc path and exit from
  // GpuVkState::tritest. Without this call the documented knobs are silently
  // ignored and a supposed renderer self-test boots the game instead.
  game->gpu_vk.tritest(); // PSXPORT_GPU_SELFTEST=1; optional painter extension

  watchdog_init(); // PSXPORT_WATCHDOG=<sec>: abort + backtrace if a frame stalls

  load_exe(path, c);

  void gte_init(void);
  void threads_init(Core *);
  void threads_register_overrides(void);

  gte_init();                  // GTE (COP2) geometry coprocessor
  mdec_init();                 // MDEC video decoder
  spu_init();                  // SPU audio core
  game->spu_audio.init();      // SDL audio sink (PSXPORT_NOAUDIO=1 to disable)
  game->gpu.gpu_native_init(); // native GPU renderer (parses the guest's GP0 stream)
  // Native CD: installs handlers at the guest chokepoints named in GameConfig's cd* group, so CD
  // commands ACK and reads are served from the disc image instead of spinning on a controller we do
  // not model. Registration goes through PlatformHle::register_, which validates each address
  // against GameConfig::hle's window — so a cd* address outside that window is REFUSED (loudly),
  // not silently dropped. Must run before initBuiltins' count is read, and before any guest code
  // touches the CD.
  game->cd.overridesInit();
  // Native non-stall owners cover the measured CD/MDEC waits. VSync is deliberately different:
  // initBuiltins installs its fatal product-contract trap.
  game->platform_hle.initBuiltins();
  spyro_register_field_scheduler();
  threads_init(c); // native BIOS threads (ucontext); main = slot 0
  threads_register_overrides();

  // PSXPORT_SELFTEST=<name> — run a self-test INSTEAD of the game and exit with its status.
  // Dispatched here: after the hardware backends are up (a self-test drives them directly) and
  // before the guest boots. This port had no such call at all, so psxport's whole self-test suite —
  // startgame, narration, oracle, oraclediff, mdecpump, spuirq — was unreachable from Spyro and
  // could never report anything. See external/psxport/runtime/recomp/selftest.cpp.
  if (const char *which = cfg_str("PSXPORT_SELFTEST")) {
    if (*which) {
      return selftest_run(path);
    }
  }

  // THE PRODUCER DB'S `begin`, and it must be HERE before the first native boot field. The
  // framework calls this from its own frame loop, which this port never enters — so without this
  // line the census ran armed and fed for the whole run and then emitted nothing at all (issue
  // #58). An uncapped interactive shell has no natural final frame; the title field scheduler's
  // cap and producer_run.cpp's exit path create one. See producer_run.h.
  spyro_producer_run_begin(c);

  c->r[4] = 1;
  c->r[5] = 0; // a0/a1 as the BIOS would leave them (minimal)

  // Boot returns at the native frame-loop boundary. The framework shell owns iteration and calls
  // exactly one finite title-owned FrameDriver step; it does not wrap title services around it.
  dc_boot_init(c);

  for (uint32_t frame = 1;; ++frame) {
    dc_step_frame(c, frame);
  }

  return 0; // unreachable: gpu_present owns the window-close exit
}
