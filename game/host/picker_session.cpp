#include "picker_session.h"

#include "panel_sessions.h"
#include "title_session.h"

#include "cfg.h"
#include "choice_view.h"
#include "core.h"
#include "dbg_server.h"
#include "frame_pacer.h"
#include "game.h"
#include "gpu_vk.h"
#include "gpu_vk_device.h"

#include <lucent/log.h>

#include <cstdlib>
#include <optional>
#include <string>

namespace spyro {
namespace {

// Active-low pad bits, as Pad::buttons carries them.
constexpr std::uint16_t kPadLeft = 0x0100u;
constexpr std::uint16_t kPadRight = 0x0200u;
constexpr std::uint16_t kPadStart = 0x0008u;
constexpr std::uint16_t kPadCross = 0x4000u;
constexpr std::uint16_t kPadConfirm = kPadCross | kPadStart;

// The panel widths: the selected panel takes half the width and each other panel a quarter. These
// are RATIOS and not three numbers — PickerLayout normalises by the total, so one panel and two
// panels fill the same width by the same rule.
constexpr float kSelectedShare = 0.5f;
constexpr float kUnselectedShare = 0.25f;
// A tenth of the surface WIDTH of slant. The panels are PARALLELOGRAMS, which is what separates
// them: the divider between two panels is one line, cut the same way by both neighbours. The lean
// is a horizontal displacement, so it is a fraction of the width; a twentieth of the height was a
// few dozen pixels on a 1280-wide window, which is a hairline rather than a slant.
constexpr float kSlantPerWidth = 0.1f;
// The width animation closes a quarter of what is left per frame, so a selection change settles in
// about a dozen frames — long enough to read as movement, short enough that the panel is where the
// player left it before they can press again.
constexpr float kWidthResponse = 0.25f;
// While a panel shows nothing yet it gets this many product steps per picker frame instead of one,
// so every title reaches a visible picture in a bounded stretch of wall time instead of one title
// waiting for the other two to finish. Bounded, because an unbounded boot would freeze the screen
// on the panel the player is trying to leave.
constexpr int kBootStepBudget = 6;
} // namespace

PickerSession::PickerSession(PickerRuntime &runtime,
                             std::span<const TitleAvailability> titles,
                             const PickerContent &content,
                             int frameCap,
                             GpuDevice &presentation)
    : runtime_(runtime), titles_(titles), content_(content), frameCap_(frameCap),
      presentation_(presentation),
      layout_(
          content.panelCount(), kSelectedShare, kUnselectedShare, kSlantPerWidth, kWidthResponse) {
  if (content.panelCount() > PickerLayout::maxPanels()) {
    lucent::error("picker",
                  "{} titles are available and the picker lays out at most {} panels",
                  content.panelCount(),
                  PickerLayout::maxPanels());
    std::abort();
  }
  panels_ = std::make_unique<PanelSessions>(titles_, presentation_);
  runtime_.bind(&content_, nullptr);
}

PickerSession::~PickerSession() = default;

int PickerSession::serviceInput(Game &host) {
  // The host's pad is the ONLY pad that sees the player: every panel session's pad has the player's
  // input suppressed, so a Left tap moves ONE selection instead of three demos at once.
  psx::ui::ChoiceView *screen = host.rml_overlay.choiceScreen();
  if (host.pad.pressedButton(kPadLeft) && screen != nullptr) {
    screen->move(-1);
  }
  if (host.pad.pressedButton(kPadRight) && screen != nullptr) {
    screen->move(+1);
  }
  // The screen's navigator is the authority on WHERE the selection is, so a D-pad press and a
  // `select <slug>` on the control channel land in exactly the same place.
  int panel = panels_->selected();
  if (screen != nullptr && screen->selectedIndex() >= 0) {
    panel = screen->selectedIndex();
  }
  if (std::optional<int> requested = runtime_.takeSelection()) {
    panel = *requested;
  }
  if (panels_->count() > 0) {
    panels_->select(panel);
    if (screen != nullptr && screen->selectedIndex() != panels_->selected()) {
      screen->select(panels_->selected());
    }
  }
  runtime_.setSelection(panels_->selected());

  int chosen = -1;
  if (host.pad.pressedButton(kPadConfirm) && panels_->count() > 0) {
    confirmMask_ = host.pad.buttons & kPadConfirm;
    chosen = panels_->selected();
  }
  if (std::optional<std::string> slug = runtime_.takePick()) {
    std::string refusal;
    if (const TitleAvailability *title = content_.findAvailable(*slug, refusal)) {
      const int requested = panels_->panelOf(title->identity->slug);
      if (requested >= 0) {
        panels_->select(requested);
        // A headless `pick` has no button behind it, so the game receives Cross — the button the
        // player would have pressed — and the demo leaves for its title screen exactly as it does
        // on the disc.
        confirmMask_ = kPadCross;
        chosen = requested;
      }
    } else {
      lucent::warn("picker", "pick refused: {}", refusal);
    }
  }
  return chosen;
}

PickerSession::Result PickerSession::run() {
  psxport_install_game(runtime_);
  // The host's own session, in which no guest runs. It exists because the window's control
  // endpoint, the pad the player is read through and the screen text all need an owner, and none of
  // the panel sessions may have them: a panel is a title's picture, not the picker.
  auto host = std::make_unique<Game>(presentation_);
  Core &core = host->core;
  host->pad.useLiveInputOnly(); // a selector must not open or rotate the player's pad recording
  const int frameCap = host->dbg_server.attach(&core, frameCap_);
  gpu_vk_ensure_device(&core);

  composite_ = std::make_unique<PickerComposite>(presentation_, &core);
  runtime_.bind(&content_, composite_.get());

  psx::ui::ChoiceView *screen = host->rml_overlay.showChoiceScreen(content_.panelContent());
  if (screen == nullptr) {
    lucent::error("picker", "the selector screen could not be created");
    return {};
  }
  // The screen's NAVIGATOR, never its artwork. It is what moves the selection and what the host's
  // own `select` moves, so the pad and the control channel resolve the panel through one object;
  // but it is never recorded into a GPU pass, so not one glyph of it reaches the window. The
  // selector used to write the selected title's name and a control hint over three running demos;
  // the panels are named by each title's OWN logo instead (panel_logo.h) — the game's own artwork,
  // from the player's own disc, rather than a caption describing a picture that is already there.
  screen->setEntriesVisible(false);
  screen->setBackdropOpaque(false);

  int frame = 0;
  for (; frameCap == 0 || frame < frameCap; ++frame) {
    host->dbg_server.honourPause(&core);
    host->pad.serviceFrame();

    int sinkWidth = 0, sinkHeight = 0;
    gpu_vk_present_sink_size(&sinkWidth, &sinkHeight);
    layout_.setSurface(sinkWidth, sinkHeight);
    layout_.setSelection(panels_->selected());
    composite_->setSurface(layout_, sinkWidth, sinkHeight);
    // Every panel session presents into the composite, never into the window: the window belongs to
    // this host, and each session's picture is a fraction of it.
    for (int panel = 0; panel < panels_->count(); ++panel) {
      if (TitleSession *session = panels_->session(panel)) {
        session->presentToPane(composite_->paneImageWidth(), composite_->paneImageHeight());
      }
    }

    const int chosen = serviceInput(*host);
    if (chosen >= 0 && panels_->count() > 0) {
      std::unique_ptr<TitleSession> session = panels_->confirm();
      if (session != nullptr) {
        // The press that chose this panel REACHES the game it chose, as a real pad press: the demo
        // leaves for its title screen the way it does on the disc instead of being jumped past it.
        session->pressOnce(confirmMask_);
        lucent::info("picker",
                     "{} confirmed at panel {} — it continues into the window",
                     content_.panelTitle(chosen).identity->slug,
                     chosen);
        composite_.reset();
        return Result{Outcome::Chosen, std::move(session)};
      }
    }

    // ONE guest advances this frame, and the panel animation runs on the same frame: the width
    // change the player just asked for is drawn from its first frame, not after it has settled.
    panels_->advance(cfg_int("PSXPORT_PICKER_BOOT_STEPS", kBootStepBudget));
    layout_.advance();
    panels_->report();

    sources_.clear();
    for (int panel = 0; panel < panels_->count(); ++panel) {
      PanelSource source;
      source.session = panels_->core(panel);
      // The picture's OWN aspect, so the panel cover-crops to the shape it really is: a 4:3 game in
      // a tall panel is cropped at the sides, a widened one is cropped top and bottom, and neither
      // leaves a band of background for the player to read as a mistake.
      source.pictureWidth = panels_->pictureWidth(panel);
      source.pictureHeight = panels_->pictureHeight(panel);
      source.logo = panels_->logo(panel);
      sources_.push_back(source);
    }
    composite_->present(layout_, sources_, panels_->selected());
    host_screen_pace(&core);
    host->dbg_server.service(&core);
  }
  lucent::info("picker", "selector ended after {} frame(s) with no choice", frame);
  return {};
}

} // namespace spyro
