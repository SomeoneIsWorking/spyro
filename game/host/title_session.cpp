#include "title_session.h"

#include "cfg.h"
#include "core.h"
#include "dbg_server.h"
#include "frame_loop_shell.h"
#include "game.h"
#include "game_runtime.h"
#include "gpu_vk.h"
#include "gpu_vk_internal.h"
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

#include <cstdlib>

extern "C" {
void watchdog_init(void);
void watchdog_disable(void);
void mdec_init(void);
void spu_init(void);
}

void dc_boot_init(Core *c);
void dc_step_frame(Core *c, uint32_t frame);

namespace spyro {

// The session's state that needs the framework's headers. Hidden behind Parts so this header stays
// the session's CONTRACT: a caller that owns a TitleSession needs to know what it can ask of one,
// and nothing about how a Game is wired.
struct TitleSession::Parts {
  // The Game is built in boot(), NOT here: a Core snapshots the process's installed GameRuntime
  // when it is constructed, so the title's runtime must be installed immediately before its own
  // Game exists and not one session earlier.
  std::unique_ptr<Game> game;
  bool booted = false;
};

TitleSession::TitleSession(const TitleAvailability &title,
                           bool selectorAvailable,
                           GpuDevice &presentation)
    : title_(title), selectorAvailable_(selectorAvailable), presentation_(presentation),
      parts_(std::make_unique<Parts>()) {}

TitleSession::~TitleSession() {
  if (!parts_->booted) {
    return;
  }
  // The title is over: the alarm that guards its frames must not outlive it into the next session
  // or into the selector.
  watchdog_disable();
  psx::cpu::shutdownHostTurn(parts_->game->core);
  reportRuntimeRun(parts_->game->core, steps_);
  // The per-class drawn-reach census covers every field this session ran (issue 0154).
  margin_object_census::writeReportIfRequested(spyro::context(parts_->game->core).marginCensus);
}

bool TitleSession::boot() {
  if (parts_->booted) {
    return true;
  }
  SpyroRuntime &runtime = runtimeFor(title_.identity->title);
  psxport_install_game(runtime);
  parts_->game = std::make_unique<Game>(presentation_);
  Game &game = *parts_->game;
  Core &core = game.core;
  game.session.setReturnAvailable(selectorAvailable_);
  // THE PANEL-ONLY OVERRIDES BELOW ARE PANEL-ONLY. A session in the selector is a PICTURE in a
  // panel: it presents into the compositor's surface rather than the window, the picker owns the
  // player's input so a background guest never sees it, and only the panel the player is looking at
  // is heard. A session that IS the product — the maintainer override `spyro_port <executable>` —
  // must behave exactly as it did before any of this existed: windowed, with the player's pad live
  // and its audio on. Applying the panel defaults to it is not a cosmetic difference: a title whose
  // boot waits on its own audio device never gets there, and the run stalls in the first guest step
  // with no frame ever presented. They are therefore applied exactly where they mean something.
  if (selectorAvailable_) {
    applyPresentRoute();
    // The picker owns the player's input; a panel's guest must not see it (see setPlayerInput).
    game.pad.setPlayerInputSuppressed(!playerInput_);
    // Only the session the player is looking at is heard (see setAudible).
    game.spu_audio.setOutputEnabled(audible_);
  }

  watchdog_init();
  load_exe(title_.executable.c_str(), &core);
  // Power THIS Game's peripherals: gte_init/spu_init act on whatever is currently bound, and in a
  // process that already ran a title that is not this Game until it is bound. They are POWER, done
  // ONCE — the per-frame-step rebind that names the session actually running is the frame loop's
  // own job (FrameLoopShell::step), so no spine has to remember to do it.
  gte_bind(&core);
  spu_bind(&core);
  mdec_bind(&core);
  xa_bind(&core);
  gte_init();
  mdec_init();
  spu_init();
  game.spu_audio.init();
  game.gpu.gpu_native_init();

  // The live debug endpoint is a framework service the title-owned spine attaches itself; the
  // returned cap is 0 for a client-driven run (see DbgServer::attach). Several sessions in one
  // process each attach; only the first owns the host port, which is what lets the picker and a
  // running title share one control channel.
  // The returned cap is 0 for a client-driven run (see DbgServer::attach), and THAT is the cap the
  // frame loop is given: reading the configuration a second time would put a hard frame limit on
  // every session in a process whose channel a client is driving, which is the opposite of what a
  // driven run is for.
  const int frameCap = game.dbg_server.attach(&core, cfg_int("PSXPORT_NATIVE_FRAMES", 0));
  // PSXPORT_STORE_OBSERVE belongs to this spine for the same reason: native_boot_run never runs
  // here.
  store_observe_attach(core);
  runtimeRun(core) = RuntimeRun(frameCap);

  dc_boot_init(&core);
  parts_->booted = true;
  lucent::info("session",
               "{} ({}) booted{}",
               title_.identity->displayName,
               title_.identity->serial,
               paused_ ? " — paused, waiting in its picker panel" : "");
  return true;
}

void TitleSession::step() {
  if (!parts_->booted || paused_) {
    return;
  }
  Game &game = *parts_->game;
  if (game.session.returnRequested()) {
    end_ = End::ReturnedToSelector;
    return;
  }
  game.dbg_server.honourPause(&game.core);
  if (runtimeRun(game.core).shouldEnd()) {
    end_ = End::Finished;
    return;
  }
  dc_step_frame(&game.core, static_cast<uint32_t>(++steps_));
  game.dbg_server.service(&game.core);
}

bool TitleSession::returnRequested() const {
  return parts_->booted && parts_->game->session.returnRequested();
}

Core &TitleSession::core() {
  if (!parts_->game) {
    lucent::error(
        "session",
        "{} asked for its Core before boot() — a session has no machine until it has booted",
        title_.identity->slug);
    std::abort();
  }
  return parts_->game->core;
}

bool TitleSession::showsTitlePicture() const {
  if (!parts_->booted || parts_->game == nullptr) {
    return false;
  }
  // The title's own driver, asked directly. A panel that showed a publisher's logo because the logo
  // passed a pixel test was the wrong answer twice over: the logo is boot, and it is not the
  // picture the panel advertises. The driver knows when the retail boot prefix has returned.
  const FrameDriver &driver = FrameLoopShell{}.requireDriver(*parts_->game);
  if (!driver.pastBootPrefix()) {
    return false;
  }
  return true;
}

bool TitleSession::hasPicture() const {
  return parts_->booted && parts_->game->gpu_vk.lastPresented().valid();
}

void TitleSession::refreshPicture() const {
  if (!parts_->booted) {
    return;
  }
  // Asked at most every few steps: each ask reads the presented image back off the GPU, and a boot
  // runs for thousands of steps of which the first ones are all black anyway.
  constexpr std::uint64_t kProbeEverySteps = 12;
  if (steps_ != 0 && steps_ - contentCheckedAtStep_ < kProbeEverySteps) {
    return;
  }
  contentCheckedAtStep_ = steps_;
  // The probe also HOLDS the frame. It is asked again for as long as this session is running, and
  // it is NOT asked again once the session is paused: it used to cache "this panel has content"
  // forever, which froze every panel on the first picture it ever had, and asking a frozen panel
  // forever is the other side of that coin — a readback per panel per visit for a picture that
  // cannot change. The probe itself is cheap between visits: once per new present, and a held frame
  // is replaced at most every fifteen of the session's own frames.
  contentSeen_ = parts_->game->gpu_vk.retainFilledPresentImage();
}

int TitleSession::pictureWidth() const {
  const GpuVkState::PresentedImage image = presentedPicture();
  return pictureSize(image).width;
}

int TitleSession::pictureHeight() const {
  const GpuVkState::PresentedImage image = presentedPicture();
  return pictureSize(image).height;
}

// The shape the panel must be cropped to: the picture's own content rect where one has been
// measured, the viewport before that. It must be the SAME rect the compositor samples, or the panel
// crops to one aspect and draws another.
TitleSession::PaneSize TitleSession::pictureSize(const GpuVkState::PresentedImage &image) const {
  if (!image.valid()) {
    return PaneSize{1, 1};
  }
  const PaneRect picture =
      image.content.w > 0 && image.content.h > 0 ? image.content : image.viewport;
  return PaneSize{picture.w, picture.h};
}

// What this session's pane draws: the held frame when there is one, because that is what the
// compositor samples, and its viewport is therefore the aspect the panel must be cropped to.
GpuVkState::PresentedImage TitleSession::presentedPicture() const {
  if (!parts_->booted) {
    return {};
  }
  GpuVkState::PresentedImage image = parts_->game->gpu_vk.lastFilledPresented();
  if (!image.valid()) {
    image = parts_->game->gpu_vk.lastPresented();
  }
  return image;
}

void TitleSession::applyPresentRoute() {
  if (!parts_->game) {
    return; // no machine yet; boot() applies it
  }
  if (paneDestination_) {
    gpu_vk_present_to_pane(&parts_->game->core, paneImageW_, paneImageH_);
  } else {
    gpu_vk_present_to_window(&parts_->game->core);
  }
}

void TitleSession::presentToPane(int imageWidth, int imageHeight) {
  paneDestination_ = true;
  paneImageW_ = imageWidth;
  paneImageH_ = imageHeight;
  applyPresentRoute();
}

void TitleSession::presentToWindow() {
  paneDestination_ = false;
  paneImageW_ = 0;
  paneImageH_ = 0;
  applyPresentRoute();
}

void TitleSession::setPlayerInput(bool live) {
  playerInput_ = live;
  if (parts_->booted) {
    parts_->game->pad.setPlayerInputSuppressed(!live);
  }
}

void TitleSession::pressOnce(std::uint16_t activeLowMask, int frames) {
  if (!parts_->booted || activeLowMask == 0xFFFFu) {
    return;
  }
  parts_->game->pad.driveTap(activeLowMask, frames);
}

void TitleSession::pause() {
  if (paused_ || !parts_->booted) {
    paused_ = true;
    return;
  }
  paused_ = true;
  // The watchdog guards frames of the session that is RUNNING. A paused session presents nothing,
  // so leaving its alarm armed would trip on a pause the player chose.
  watchdog_disable();
}

void TitleSession::resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;
  if (parts_->booted) {
    watchdog_init();
  }
}

void TitleSession::setAudible(bool audible) {
  audible_ = audible;
  if (parts_->booted) {
    parts_->game->spu_audio.setOutputEnabled(audible);
  }
}

void TitleSession::claimDebugEndpoint() {
  if (!parts_->booted) {
    return;
  }
  if (!parts_->game->dbg_server.claimEndpoint()) {
    lucent::warn("host",
                 "another session still holds the debug endpoint; {} cannot claim it",
                 title_.identity->displayName);
  }
}

} // namespace spyro
