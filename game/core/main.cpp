// game/core/main.cpp — the Spyro port's process entry point.
//
// main() is game-side: it installs the game seam (GameConfig + GameHooks + RecompRegistry), brings up
// the framework's PSX hardware backends, loads the executable, and boots it. After the installs it
// touches only framework symbols.
#include "core.h"
#include "game.h"
#include "cfg.h"
#include "fs_util.h"
#include "platform_hle.h"
#include "spyro_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
  void watchdog_init(void); void mdec_init(void); void spu_init(void);
}

void load_exe(const char* path, Core* c);   // framework: runtime/recomp/boot.cpp
void dc_boot_init(Core* c);                 // framework: crt0_setup + game_init (-> hooks->bootInit)

// Spyro boots straight from SCUS_942.28 — unlike Tomba!2 there is no separate SCEA boot stub that
// LoadExec's a MAIN.EXE, so there is no stub stage to run and no second image to load.
static const char* kDefaultExe = "scratch/bin/spyro/SCUS_942.28";
static const char* kDiscExePath = "SCUS_942.28";   // its path on the disc (root directory)

int main(int argc, char** argv) {
  // Install the seam BEFORE the first Core exists: Core's ctor snapshots psxport_game_config() and
  // psxport_game_hooks() into c->cfg / c->hooks, and the substrate reads c->cfg->field for every
  // guest-address literal.
  spyro_install_game_config();
  spyro_install_recomp();

  const char* path = argc > 1 ? argv[1] : kDefaultExe;

  Game* game = new Game();
  Core* c = &game->core;

  // Self-provision the executable: with just a disc image (PSXPORT_SPYRO_DISC, .env, or a *.chd in
  // the working directory) the binary runs directly, no prior ./run.sh extraction needed.
  if (!Fs::exists(path)) {
    cfg_logw("boot", "%s missing — extracting from disc", path);
    if (!disc_extract_file(&game->disc, kDiscExePath, path)) {
      cfg_loge("boot", "extraction failed: provide a disc (PSXPORT_SPYRO_DISC, .env, or a *.chd in "
                       "the working directory) or run ./run.sh");
      return 1;
    }
  }

  watchdog_init();          // PSXPORT_WATCHDOG=<sec>: abort + backtrace if a frame stalls

  load_exe(path, c);

  void gte_init(void);
  void threads_init(Core*);
  void threads_register_overrides(void);

  gte_init();                        // GTE (COP2) geometry coprocessor
  mdec_init();                       // MDEC video decoder
  spu_init();                        // SPU audio core
  game->spu_audio.init();            // SDL audio sink (PSXPORT_NOAUDIO=1 to disable)
  game->gpu.gpu_native_init();       // native GPU renderer (parses the guest's GP0 stream)
  game->platform_hle.initBuiltins(); // HW sync/wait stalls -> native non-stall (VSync/CdSync/MDEC)
  threads_init(c);                   // native BIOS threads (ucontext); main = slot 0
  threads_register_overrides();

  c->r[4] = 1; c->r[5] = 0;          // a0/a1 as the BIOS would leave them (minimal)

  // Boot: bind the per-core hardware, register overrides (none yet), run crt0, then enter the guest's
  // main() via the bootInit hook. In Phase 0 the guest owns its own frame loop, so this does not
  // return until the game exits — see game/core/game_hooks.cpp for why that is the correct Phase-0
  // behaviour and what has to be RE'd before the native frame loop can take over.
  dc_boot_init(c);

  cfg_logi("boot", "guest main() returned");
  return 0;
}
