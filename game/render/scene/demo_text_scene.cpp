#include "demo_text_scene.h"

#include "core.h"
#include "guest_globals.h"

#include <algorithm>
#include <vector>

namespace spyro::demo_text_scene {
namespace {

// 0x80010AC0, verified byte-for-byte against SCUS_942.28 ("DEMO MODE\0").
constexpr const char *kText = "DEMO MODE";
constexpr hud_text::Point3 kPosition{199, 200, 4352};
constexpr hud_text::Point3 kSpacing{16, 1, 5120};
constexpr std::int32_t kSpaceWidth = 18;
constexpr std::uint8_t kShadeIndex = 2u;
// 0x8001898C-0x800189A0: the phase is g_LevelTicks * 4.
constexpr std::int32_t kWobblePhaseScale = 4;

} // namespace

Plan plan(std::uint32_t demoMode) {
  if (demoMode == 0u) {
    return {};
  }
  return {.armed = true,
          .layout = hud_text::layoutCaption(kText, kPosition, kSpacing, kSpaceWidth),
          .shadeIndex = kShadeIndex};
}

bool submit(Core *core) {
  const Plan caption = plan(core->mem_r32(spyro::guest::kDemoMode));
  if (!caption.armed) {
    return true;
  }
  const std::size_t glyphs = caption.layout.glyphs.size();
  if (!hud_text::fits(core, glyphs) || !hud_text::shadedQueueFits(core, glyphs)) {
    return false;
  }
  const auto written = hud_text::append(core, caption.layout, caption.shadeIndex);
  hud_text::wobble(
      core, written, (std::int32_t)core->mem_r32(spyro::guest::kLevelTicks) * kWobblePhaseScale);
  // 0x80018880 copies the arena from the cursor UP to its end, so the queue holds the glyphs
  // newest first: the reverse of the order the builder wrote them in.
  std::vector<std::uint32_t> newestFirst(written.begin(), written.end());
  std::reverse(newestFirst.begin(), newestFirst.end());
  return hud_text::enqueueShaded(core, newestFirst);
}

} // namespace spyro::demo_text_scene
