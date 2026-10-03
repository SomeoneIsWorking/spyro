#pragma once

#include "title_availability.h"

#include "host_presentation.h"

#include <filesystem>
#include <memory>
#include <span>

namespace spyro {

class TitleSession;

// The product's top level: the title selector, then the chosen title, then the selector again, all
// in this one process. An explicit executable (a maintainer override) skips the selector and runs
// that one title, which is the only way the process runs a single title.
//
// IT OWNS THE WINDOW. `presentation_` is the process's one window and one SDL_GPU device, created
// before the first session and released after the last, and every session presents through it. It
// was a `Game` member instead, so picking a title destroyed the selector's window and device and
// built a second beside it — which is what made the selector look like a separate program the
// player had to close. Session teardown still releases everything session-owned (the machine, the
// pad, the debug endpoint, the memory card); nothing session-owned here reaches this object.
//
// The selector and a title are the SAME session here: confirming hands back the live session whose
// demo was playing in the panel, so the chosen title continues from the frame the player saw rather
// than booting again from power-on.
class ProductHost {
public:
  explicit ProductHost(std::filesystem::path provisioningRoot);

  int runSelector();
  int runExecutable(const std::filesystem::path &executable);

private:
  // Run one session to its end, full-window, until it finishes or asks for the selector back. The
  // one loop every entered title runs in, whether it was started from the picker or from an
  // explicit executable argument.
  int runToEnd(TitleSession &session);

  std::filesystem::path root_;
  psxport::HostPresentation presentation_;
};

} // namespace spyro
