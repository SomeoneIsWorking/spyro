#include "picker_session.h"

#include "core.h"
#include "dbg_server.h"
#include "frame_pacer.h"
#include "game.h"
#include "gpu_vk.h"
#include "rmlui_overlay.h"

#include <lucent/log.h>
#include <memory>

namespace spyro {

namespace {
// Active-low pad bits, as Pad::buttons carries them.
constexpr std::uint16_t kPadUp = 0x0010u;
constexpr std::uint16_t kPadDown = 0x0040u;
constexpr std::uint16_t kPadStart = 0x0008u;
constexpr std::uint16_t kPadCross = 0x4000u;
} // namespace

PickerSession::PickerSession(PickerRuntime &runtime, const PickerContent &content, int frameCap)
    : runtime_(runtime), content_(content), frameCap_(frameCap) {}

PickerSession::Result PickerSession::run() {
  runtime_.bind(&content_);
  psxport_install_game(runtime_);
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  // The picker is not a play session: it must not open (or rotate) the default pad recording.
  game->pad.useLiveInputOnly();
  const int frameCap = game->dbg_server.attach(&core, frameCap_);
  gpu_vk_ensure_device(&core);

  psx::ui::ChoiceView *screen = game->rml_overlay.showChoiceScreen(content_.content());
  if (!screen) {
    lucent::error("picker", "the selector screen could not be created");
    return {};
  }

  Result result;
  for (int frame = 0; frame < frameCap || frameCap == 0; ++frame) {
    game->dbg_server.honourPause(&core);
    game->pad.serviceFrame(); // also samples the button edges
    std::optional<int> pick;
    if (game->pad.pressedButton(kPadUp)) {
      screen->move(-1);
    }
    if (game->pad.pressedButton(kPadDown)) {
      screen->move(+1);
    }
    if (game->pad.pressedButton(kPadCross) || game->pad.pressedButton(kPadStart)) {
      pick = screen->activate();
    }
    if (std::optional<int> clicked = screen->takeClick()) {
      pick = clicked;
    }
    if (std::optional<std::string> slug = runtime_.takePick()) {
      std::string refusal;
      if (const TitleAvailability *title = content_.findAvailable(*slug, refusal)) {
        result = {Outcome::Chosen, title};
        break;
      }
    }
    if (pick) {
      result = {Outcome::Chosen, &content_.title(*pick)};
      break;
    }
    gpu_vk_present_screen(&core);
    host_screen_pace(&core);
    game->dbg_server.service(&core);
  }
  lucent::info("picker",
               "selector ended after the screen: {}",
               result.title ? std::string(result.title->identity->slug) : std::string("no choice"));
  return result;
}

} // namespace spyro
