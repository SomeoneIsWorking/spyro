#pragma once

#include "game_runtime.h"
#include "picker_content.h"

#include <optional>
#include <string>

namespace spyro {

class PickerComposite;

// The GameRuntime of the selector screen: the host's own session, in which no GUEST runs. Its job
// is the control channel — which lists the entries (`picker`), takes one (`pick <slug>`), moves the
// selection (`select <slug>`, `select left|right`) and writes the composited frame to a file
// (`picker shot <path>`) — the headless route to every decision the pad makes in the window.
//
// It also owns the screen's own `Game`: the sessions on screen are three other Games, and this is
// the one that holds the control endpoint, reads the pad and records the screen text.
class PickerRuntime final : public GameRuntime {
public:
  PickerRuntime() = default;

  void bind(const PickerContent *content, PickerComposite *composite) {
    content_ = content;
    composite_ = composite;
    pendingSlug_.reset();
    pendingSelection_.reset();
  }
  // The slug `pick` accepted since the last call: the title to start, as opposed to the panel to
  // show.
  std::optional<std::string> takePick() {
    std::optional<std::string> pick = std::move(pendingSlug_);
    pendingSlug_.reset();
    return pick;
  }
  // The panel `select` asked for since the last call, as an index into the PANELS (the available
  // titles in catalog order). Nullopt when nothing was asked for.
  std::optional<int> takeSelection() {
    std::optional<int> selection = pendingSelection_;
    pendingSelection_.reset();
    return selection;
  }

  void *createContext(Core &) override {
    return nullptr;
  }
  void destroyContext(void *) override {}
  void registerOverrides(Game &) override {}
  void bootInit(Core &) override {}
  RenderCapabilities renderCapabilities() const override {
    return RenderCapabilities::direct();
  }
  bool guestVramIsPicture(const Game &) const override {
    return false;
  }
  // `line` is the whole command line, as the framework passes it.
  bool controlCommand(Core &core, const char *cmd, const char *line, FILE *out) override;

  // The command logic itself, which needs no Core: a hermetic test drives it directly.
  bool handle(const char *cmd, const char *line, FILE *out);
  // Where the selection is now, so `select left|right` can move it. The COUNT of panels is the
  // content's own: a host that told the runtime a different count would let it resolve a panel that
  // does not exist, which is how a control channel ends up naming a title the picker cannot show.
  void setSelection(int selected);

private:
  const PickerContent *content_ = nullptr;
  PickerComposite *composite_ = nullptr;
  std::optional<std::string> pendingSlug_;
  std::optional<int> pendingSelection_;
  int selected_ = 0;
};

} // namespace spyro
