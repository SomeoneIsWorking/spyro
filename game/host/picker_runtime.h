#pragma once

#include "game_runtime.h"
#include "picker_content.h"

#include <optional>
#include <string>

namespace spyro {

// The GameRuntime of the selector screen: no guest runs, so every guest-facing seam is empty. Its
// only product is the control channel, which lists the entries (`picker`) and takes one (`pick
// <slug>`), the headless route to the same decision a pad confirm makes.
class PickerRuntime final : public GameRuntime {
public:
  PickerRuntime() = default;

  void bind(const PickerContent *content) {
    content_ = content;
    pendingSlug_.reset();
  }
  // The slug `pick` accepted since the last call.
  std::optional<std::string> takePick() {
    std::optional<std::string> pick = std::move(pendingSlug_);
    pendingSlug_.reset();
    return pick;
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

private:
  const PickerContent *content_ = nullptr;
  std::optional<std::string> pendingSlug_;
};

} // namespace spyro
