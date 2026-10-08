#include "guest_widescreen_owner.h"

#include "ui_anchor.h"

#include "core.h"
#include "game.h"
#include "gpu_native_internal.h"
#include "gpu_vk.h"
#include "guest_widescreen_math.h"
#include "guest_widescreen_projection.h"
#include "mods.h"
#include "native_execution.h"
#include "proj_params.h"
#include "spyro_context.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro {
namespace {

namespace math = guest_widescreen_math;

// The retail bodies. Both are `mtc2 rt, CR` forms whose GTE register number is in bits 15-11, not
// bits 4-0 as every ordinary COP2 form puts it, which is why an ordinary scan finds nothing.

// SetGeomScreen(h): retail moves $a0 into CR26 and returns. `PlatformHlePlan` has a handler for
// this address that also records the projection the guest stated; this is that handler with the
// plan applied afterwards.
void setGeomScreen(Core *core) {
  libgte_set_geom_screen(core, static_cast<std::int32_t>(core->r[4]));
  GuestWidescreenOwner::require(*core).published(*core, ProjectionSite::SetGeomScreenLeaf);
}

// SetGeomOffset(x, y): retail shifts both arguments into 16.16, moves them into CR24/CR25 and
// leaves the shifted values in the argument registers, which the caller observes.
void setGeomOffset(Core *core) {
  const auto x = static_cast<std::int32_t>(core->r[4]);
  const auto y = static_cast<std::int32_t>(core->r[5]);
  core->r[4] = static_cast<std::uint32_t>(x) << 16;
  core->r[5] = static_cast<std::uint32_t>(y) << 16;
  libgte_set_geom_offset(core, x, y);
  GuestWidescreenOwner::require(*core).published(*core, ProjectionSite::SetGeomOffsetLeaf);
}

// CR24 and CR25 are OFX and OFY, SIGNED 16-bit fields in the upper half of their control register.
std::int32_t gteOffsetX() {
  return static_cast<std::int32_t>(gte_read_ctrl(24) >> 16);
}
std::int32_t gteOffsetY() {
  return static_cast<std::int32_t>(gte_read_ctrl(25) >> 16);
}

} // namespace

GuestWidescreenOwner *GuestWidescreenOwner::of(Core &core) noexcept {
  // No abort, no log: every caller treats null as "this Core is not widening".
  return static_cast<GuestWidescreenOwner *>(spyro::context(core).projectionHook);
}

GuestWidescreenOwner &GuestWidescreenOwner::require(Core &core) {
  GuestWidescreenOwner *owner = of(core);
  if (owner == nullptr) {
    // Reaching here means a projection leaf was reachable while its owner was not.
    lucent::error("wide", "guest projection owner reached before it was published");
    std::abort();
  }
  return *owner;
}

void GuestWidescreenOwner::registerProjectionOverrides(Core &core) {
  spyro::context(core).projectionHook = this;

  // These also sit in `PlatformHlePlan`, but the framework consults a title's image-scoped override
  // before any host-service table, so they replace the stock handlers.
  if (facts_.setGeomScreenLeaf != 0) {
    psx::cpu::installNativeOverride(
        core, facts_.setGeomScreenLeaf, facts_.setGeomScreenOverrideName, setGeomScreen);
  }
  if (facts_.setGeomOffsetLeaf != 0) {
    psx::cpu::installNativeOverride(
        core, facts_.setGeomOffsetLeaf, facts_.setGeomOffsetOverrideName, setGeomOffset);
  }

  lucent::info("wide",
               "{} projection bound at {} measured library leaf/leaves plus the {} measured inline "
               "mtc2 restatements carried by the per-field re-assertion (not intercepted: they are "
               "mid-block and a native override resumes at $ra)",
               facts_.titleName,
               facts_.setGeomScreenLeaf != 0 && facts_.setGeomOffsetLeaf != 0 ? 2 : 1,
               facts_.inlineProjectionPublicationCount);
}

void GuestWidescreenOwner::published(Core &core, ProjectionSite site) {
  ++publications_;
  if (!latched_ && !latch(core)) {
    return;
  }
  if (!plan_.widescreen()) {
    return;
  }
  // Read the guest's own values before re-asserting; OFY stays at what the guest published, so the
  // picture keeps the guest's vertical framing.
  if (!movesCentre()) {
    return;
  }
  lucent::info("wide",
               "guest published its projection at {}: OFX {} OFY {}; re-asserted centre X {}",
               siteName(site),
               gteOffsetX(),
               gteOffsetY(),
               plan_.projectionCenterX);
  applyProjection(core);
}

void GuestWidescreenOwner::onProjectionPublished(Core &core) {
  published(core, ProjectionSite::SetGeomOffsetLeaf);
}

void GuestWidescreenOwner::onField(Core &core, bool startEdge) {
  (void)startEdge; // Guarding on the boot edge disables the widening.
  if (!latched_ && !latch(core)) {
    return;
  }
  if (!plan_.widescreen()) {
    return;
  }
  if (movesCentre()) {
    applyProjection(core);
  }
  // The guest restates its own 512-dot rectangle every frame, so a rectangle widened after it has
  // finished drawing is one the next frame discards.
  if (applyDrawArea(core)) {
    ++widenedAreas_;
  }
  ++widenedSteps_;
}

void GuestWidescreenOwner::onFrameTail(Core &core) {
  if (!latched_ || !plan_.widescreen()) {
    return;
  }
  if (applyDrawArea(core)) {
    ++widenedAreas_;
  }
}

int GuestWidescreenOwner::horizontalMargin() const {
  if (!latched_ || !plan_.widescreen()) {
    return 0;
  }
  return math::guestWindowLeft(plan_.projectionExtent.width, facts_.guestOffsetX);
}

ui_anchor::Frame GuestWidescreenOwner::uiFrame() const {
  if (!latched_ || !plan_.widescreen()) {
    const auto native = facts_.nativeWidth;
    return {.authored = native, .drawn = native};
  }
  return {.authored = plan_.nativeExtent.width, .drawn = plan_.presentationExtent.width};
}

bool GuestWidescreenOwner::latch(Core &core) {
  GpuState &gpu = core.game->gpu;
  // The plan's native extent must be the extent the guest actually presents. `s_disp_w` defaults to
  // 320 and only becomes this title's real width once the guest has written GP1(0x08).
  if (!gpu.s_disp_std_seen || gpu.s_disp_w <= 0 || gpu.s_disp_h <= 0) {
    return false;
  }
  plan_ = gpu_vk_latch_guest_projection(&core,
                                        GuestProjectionGeometry{
                                            .extent = {gpu.s_disp_w, gpu.s_disp_h},
                                            .drawWidth = gpu.s_disp_w,
                                        });
  latched_ = true;
  report();
  return true;
}

void GuestWidescreenOwner::applyProjection(Core &core) {
  // Only the horizontal centre moves; the vertical centre stays as the guest published it, so the
  // vertical field of view is the console's and nothing in the picture is scaled.
  const auto centerX = plan_.projectionCenterX;
  const auto centerY = static_cast<std::int32_t>(gte_read_ctrl(25) >> 16);
  libgte_set_geom_offset(&core, centerX, centerY);
}

bool GuestWidescreenOwner::movesCentre() const {
  return plan_.projectionCenterX != plan_.nativeProjectionExtent.width / 2;
}

bool GuestWidescreenOwner::applyDrawArea(Core &core) {
  // On Record the canvas holds the margins; the guest keeps its own draw width.
  if (plan_.guestDrawWidth <= plan_.nativeGuestDrawWidth) {
    return false;
  }
  GpuState &gpu = core.game->gpu;
  const int right =
      math::widenedDrawAreaRight(gpu.s_da_x1, plan_.presentationExtent.width, facts_.nativeWidth);
  if (right == gpu.s_da_x1) {
    return false;
  }
  const int bottom = gpu.s_da_y1;
  // Re-issued through the framework's own GP0 decoder so the rectangle is recorded as a guest write
  // of the same word would be. GP0, not GP1: libgpu builds the corner word as GP1(44h..47h) and the
  // hardware decodes it as GP0 E3..E6, so sending it to `gpu_gp1` is a silent no-op.
  gpu_gp0(&core, math::drawAreaBottomRight(right, bottom));
  // Read the decoder's result back rather than assuming the write took.
  lucent::debug("wide",
                "widened draw area right {} -> {} (bottom {}); register now ({}, {})",
                right - (plan_.presentationExtent.width - facts_.nativeWidth) / 2,
                right,
                bottom,
                gpu.s_da_x1,
                gpu.s_da_y1);
  return true;
}

void GuestWidescreenOwner::report() const {
  lucent::info("wide",
               "{} widescreen owner latched from the GUEST'S OWN display mode {}x{} "
               "(guest published OFX {} OFY {}): "
               "aspect={} presentation {}x{} (margin {}px), projection {}x{} (margin {}px, centre "
               "X {}), guest draw width {} -> {}. {}",
               facts_.titleName,
               plan_.nativeExtent.width,
               plan_.nativeExtent.height,
               gteOffsetX(),
               gteOffsetY(),
               aspectName(plan_.aspect),
               plan_.presentationExtent.width,
               plan_.presentationExtent.height,
               plan_.presentationHorizontalMargin,
               plan_.nativeProjectionExtent.width,
               plan_.nativeProjectionExtent.height,
               plan_.projectionHorizontalMargin,
               plan_.projectionCenterX,
               plan_.nativeGuestDrawWidth,
               plan_.guestDrawWidth,
               plan_.widescreen()
                   ? "This plan WIDENS the projection."
                   : "This plan does NOT widen: the published projection is retail's own.");
}

const char *GuestWidescreenOwner::siteName(ProjectionSite site) const {
  switch (site) {
  case ProjectionSite::SetGeomOffsetLeaf:
    return facts_.setGeomOffsetSiteName;
  case ProjectionSite::SetGeomScreenLeaf:
    return facts_.setGeomScreenSiteName;
  }
  return "?";
}

const char *GuestWidescreenOwner::aspectName(PresentationAspect aspect) {
  switch (aspect) {
  case PresentationAspect::Standard4x3:
    return "4:3";
  case PresentationAspect::Wide16x9:
    return "16:9";
  case PresentationAspect::UltraWide21x9:
    return "21:9";
  case PresentationAspect::MatchSink:
    return "match-sink";
  }
  return "?";
}

} // namespace spyro
