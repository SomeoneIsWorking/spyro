// `wide_screen_space::drawClipRight` is the ONE drawn right edge every renderer reads, so the two
// things it must get right are both tested here: that it reports the widened width when the wide
// engine is on, and that it reports the GUEST's window when there is no Core or no Game. The second
// is not hypothetical — an open-coded call without the null guard took the process down through
// `gpu_vk_wide_engine`, which dereferences `c->game` without checking it (issue 0154).
#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "wide_clip_plan.h"
#include "wide_screen_space.h"

#include <cstdio>
#include <memory>

namespace {

int failures = 0;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    ++failures;
  }
}

} // namespace

int main() {
  using spyro::wide::kNativeClipWidth;
  using spyro::wide_screen_space::drawClipRight;
  using spyro::wide_screen_space::kGuestClipRight;

  // POSITIVE — a real Core with the wide engine on. The drawn edge is the wide engine's own width,
  // so this is the one assertion that the accessor reports the WIDENED window rather than the
  // guest's; every producer's ability to reach columns 512..683 depends on it.
  {
    auto game = std::make_unique<Game>();
    Core &core = game->core;
    core.rsub.mode.setPath(RenderPath::Native);
    // The wide width is derived from the guest's own horizontal resolution, so the fixture must
    // declare one: 512 is retail's, and the framework widens it to 684 at 16:9.
    game->gpu.s_disp_w = spyro::wide_screen_space::kGuestClipRight;
    game->mods.aspect = ASPECT_16_9;
    const int widened = gpu_vk_wide_engine_w(&core);
    expect(widened > kGuestClipRight,
           "the fixture's 16:9 width is not wider than the guest window, so the positive case "
           "would not test anything");
    expect(drawClipRight(&core) == widened,
           "a Core with the wide engine on does not report the wide engine's own width");
  }

  // NEGATIVE 1 — the same Core with the wide engine OFF. The guest's own window, so a 4:3 control
  // run measures the 4:3 answer rather than a widened one.
  {
    auto game = std::make_unique<Game>();
    Core &core = game->core;
    core.rsub.mode.setPath(RenderPath::Native);
    game->gpu.s_disp_w = spyro::wide_screen_space::kGuestClipRight;
    game->mods.aspect = ASPECT_4_3;
    expect(drawClipRight(&core) == kGuestClipRight,
           "a 4:3 Core does not report the guest's 512 window");
    expect(drawClipRight(&core) == kNativeClipWidth,
           "the guest window and the plane's native width disagree");
  }

  // NEGATIVE 2 — no Game on the Core. This is the case that SEGFAULTED: `gpu_vk_wide_engine`
  // dereferences `c->game` unconditionally, so every caller used to carry its own guard and one of
  // them forgot. The accessor owns the guard now, so a bare Core is a 4:3 answer, not a crash.
  {
    Core core{};
    core.game = nullptr;
    expect(drawClipRight(&core) == kGuestClipRight,
           "a Core with no Game did not fall back to the guest window");
  }

  // NEGATIVE 3 — no Core at all, which the recipe unit tests hand around.
  expect(drawClipRight(nullptr) == kGuestClipRight,
         "a null Core did not fall back to the guest window");

  if (failures != 0) {
    std::fprintf(stderr, "wide_screen_space: %d check(s) failed\n", failures);
    return 1;
  }
  std::printf("wide_screen_space: all checks passed\n");
  return 0;
}
