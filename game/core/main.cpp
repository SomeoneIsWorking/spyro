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
void dc_boot_init(Core *c);               // framework: crt0_setup + game_init (-> hooks->bootInit)
int selftest_run(const char *path);       // framework: runtime/recomp/selftest.cpp

// Spyro 1 boots straight from SCUS_942.28 — unlike Tomba!2 there is no separate SCEA boot stub that
// LoadExec's a MAIN.EXE, so there is no stub stage to run and no second image to load. The
// executable basename is also the serial-selection input; the bytes must then match that title's
// exact identity.
static const char *kDefaultExe = "scratch/bin/spyro/SCUS_942.28";

int main(int argc, char **argv) {
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
  game->platform_hle.initBuiltins(); // HW sync/wait stalls -> native non-stall (VSync/CdSync/MDEC)
  // Spyro's vblank timebase. Registered AFTER initBuiltins so it is visible in the same table;
  // hle.vsyncTrap stays 0 (the trap asserts the native frame loop owns timing, which is false
  // while the guest owns its own loop). See game/core/vsync.cpp.
  spyro_register_vsync(game);
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

  // THE PRODUCER DB'S `begin`, and it must be HERE: before the first frame, and before the boot
  // that never returns. The framework calls this from its own frame loop, which this port never
  // enters — so without this line the census ran armed and fed for the whole run and then emitted
  // nothing at all (issue #58). `finish` cannot go after dc_boot_init because nothing comes after
  // it; the frame cap in producer_run.cpp is what creates a last frame. See producer_run.h.
  spyro_producer_run_begin(c);

  c->r[4] = 1;
  c->r[5] = 0; // a0/a1 as the BIOS would leave them (minimal)

  // Boot: bind the per-core hardware, register overrides (none yet), run crt0, then enter the
  // port's non-returning frame loop through Spyro1Runtime::bootInit.
  dc_boot_init(c);

  lucent::info("boot", "guest main() returned");
  return 0;
}
