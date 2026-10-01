#include "title_session.h"

#include "cfg.h"
#include "core.h"
#include "dbg_server.h"
#include "game.h"
#include "host_turn.h"
#include "hw_bind.h"
#include "margin_object_census.h"
#include "psx_exe_image.h"
#include "runtime_run.h"
#include "spyro_context.h"
#include "spyro_game.h"
#include "store_observe.h"
#include "title_runtime_registry.h"

#include <lucent/log.h>
#include <memory>

extern "C" {
void watchdog_init(void);
void watchdog_disable(void);
void mdec_init(void);
void spu_init(void);
}

void dc_boot_init(Core *c);
void dc_step_frame(Core *c, uint32_t frame);

namespace spyro {

TitleSession::TitleSession(const TitleAvailability &title, bool selectorAvailable)
    : title_(title), selectorAvailable_(selectorAvailable) {}

TitleSession::End TitleSession::run() {
  SpyroRuntime &runtime = runtimeFor(title_.identity->title);
  psxport_install_game(runtime);
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  game->session.setReturnAvailable(selectorAvailable_);

  watchdog_init();
  load_exe(title_.executable.c_str(), &core);
  // Power THIS Game's peripherals: gte_init/spu_init act on whatever is currently bound, and in a
  // process that already ran a title that is not this Game until it is bound.
  gte_bind(&core);
  spu_bind(&core);
  mdec_bind(&core);
  xa_bind(&core);
  gte_init();
  mdec_init();
  spu_init();
  game->spu_audio.init();
  game->gpu.gpu_native_init();

  // The live debug endpoint is a framework service the title-owned spine attaches itself; the
  // returned cap is 0 for a client-driven run (see DbgServer::attach).
  const int frameCap = game->dbg_server.attach(&core, cfg_int("PSXPORT_NATIVE_FRAMES", 0));
  // PSXPORT_STORE_OBSERVE belongs to this spine for the same reason: native_boot_run never runs
  // here.
  store_observe_attach(core);
  runtimeRun(core) = RuntimeRun(frameCap);

  dc_boot_init(&core);

  std::uint32_t completedSteps = 0;
  End end = End::Finished;
  while (!runtimeRun(core).shouldEnd()) {
    if (game->session.returnRequested()) {
      end = End::ReturnedToSelector;
      break;
    }
    game->dbg_server.honourPause(&core);
    dc_step_frame(&core, ++completedSteps);
    game->dbg_server.service(&core);
  }
  // The title is over: the alarm that guards its frames must not outlive it into the selector.
  watchdog_disable();
  psx::cpu::shutdownHostTurn();
  reportRuntimeRun(core, completedSteps);
  // The per-class drawn-reach census covers every field this session ran (issue 0154).
  margin_object_census::writeReportIfRequested(spyro_context(core).marginCensus);
  return end;
}

} // namespace spyro
