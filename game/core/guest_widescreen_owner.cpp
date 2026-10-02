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

// ── THE TWO RETAIL BODIES
// ──────────────────────────────────────────────────────────────────────────── Each runs retail's
// own effect FIRST and asks the owner afterwards, so the widened value is what the rest of the
// frame sees even when the guest restates its projection through a leaf. Both are `mtc2 rt, CR`
// forms whose GTE register number is in bits 15-11, not in bits 4-0 the way every ordinary COP2
// form puts it, which is why an ordinary scan of an image finds nothing.

// SetGeomScreen(h). Retail moves $a0 into CR26 and returns. The framework's own `PlatformHlePlan`
// handler for this address does the same and additionally records the projection the guest stated;
// this body is that handler with the plan applied afterwards.
void setGeomScreen(Core *core) {
  libgte_set_geom_screen(core, static_cast<std::int32_t>(core->r[4]));
  GuestWidescreenOwner::require(*core).published(*core, ProjectionSite::SetGeomScreenLeaf);
}

// SetGeomOffset(x, y). Retail shifts both arguments into 16.16 and moves them into CR24/CR25, and
// leaves the shifted values in the argument registers. The shift is observable by the caller, so it
// is reproduced rather than dropped.
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
  // No abort, no log: every caller of this one treats null as "this Core is not widening", which
  // is the correct and complete answer for a Core whose library leaves have not been bound. The
  // projection leaves, which cannot run unbound, go through `require`.
  return static_cast<GuestWidescreenOwner *>(spyro_context(core).projectionHook);
}

GuestWidescreenOwner &GuestWidescreenOwner::require(Core &core) {
  GuestWidescreenOwner *owner = of(core);
  if (owner == nullptr) {
    // `registerProjectionOverrides` publishes this pointer in its first statement and installs the
    // two leaves afterwards, so reaching here means a leaf was reachable while its owner was not.
    lucent::error("wide", "guest projection owner reached before it was published");
    std::abort();
  }
  return *owner;
}

PresentationAspect GuestWidescreenOwner::presentationAspect(const Core &core) const {
  if (core.game == nullptr) {
    return PresentationAspect::Standard4x3;
  }
  switch (core.game->mods.aspect) {
  case ASPECT_4_3:
    return PresentationAspect::Standard4x3;
  case ASPECT_16_9:
    return PresentationAspect::Wide16x9;
  case ASPECT_21_9:
    return PresentationAspect::UltraWide21x9;
  case ASPECT_AUTO:
    return PresentationAspect::MatchSink;
  default:
    lucent::error(
        "wide", "{} received invalid aspect selector {}", facts_.titleName, core.game->mods.aspect);
    std::abort();
  }
}

void GuestWidescreenOwner::registerProjectionOverrides(Core &core) {
  spyro_context(core).projectionHook = this;

  // Both leaves are ALSO in `PlatformHlePlan`. That is not a conflict: the framework's own dispatch
  // contract consults a title's image-scoped override BEFORE any host-service table, so these two
  // replace the stock handlers rather than queueing behind them.
  if (facts_.setGeomScreenLeaf != 0) {
    spyro::installNativeOverride(
        core, facts_.setGeomScreenLeaf, facts_.setGeomScreenOverrideName, setGeomScreen);
  }
  if (facts_.setGeomOffsetLeaf != 0) {
    spyro::installNativeOverride(
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
  // What the GUEST published, read before this owner re-asserts over it. OFY is left at the
  // guest's own value: only the horizontal centre moves, so the picture keeps the vertical framing
  // and the object sizes the guest authored.
  lucent::info("wide",
               "guest published its projection at {}: OFX {} OFY {}; re-asserted centre X {}",
               siteName(site),
               gteOffsetX(),
               gteOffsetY(),
               math::widenedCenterX(plan_.projectionExtent.width));
  applyProjection(core);
}

void GuestWidescreenOwner::onProjectionPublished(Core &core) {
  published(core, ProjectionSite::SetGeomOffsetLeaf);
}

void GuestWidescreenOwner::onField(Core &core, bool startEdge) {
  (void)startEdge; // See the header: this owner has no boot edge, and guarding on it disables the
                   // widening.
  if (!latched_ && !latch(core)) {
    return;
  }
  if (!plan_.widescreen()) {
    return;
  }
  applyProjection(core);
  // The draw-area widening belongs at THIS edge, not at the frame tail: the guest restates its own
  // 512-dot rectangle on every frame, so a rectangle widened after the guest has finished drawing
  // is one the next frame immediately discards.
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
  // The plan's native extent must be the extent the GUEST actually presents. `s_disp_w` defaults to
  // 320 and only becomes this title's real 512-dot width once the guest has written GP1(0x08), so
  // the latch waits for that rather than deriving a plan from the default and discovering later
  // that it is NARROWER than the picture it was meant to widen.
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
  // What the guest itself left in CR24 at this point, named rather than overwritten silently: if
  // this reads the 4:3 centre the guest restated its projection during the frame that has just
  // been drawn, and the re-assertion is what puts it back -- which is the whole reason this owner
  // runs at the step boundary as well as at the publication.
  const auto centerX = math::widenedCenterX(plan_.projectionExtent.width);
  // Only the horizontal centre moves. The vertical centre and the projection distance stay at the
  // values the guest itself published, so the vertical field of view is the console's and nothing
  // in the picture is scaled.
  const auto centerY = static_cast<std::int32_t>(gte_read_ctrl(25) >> 16);
  libgte_set_geom_offset(&core, centerX, centerY);
}

bool GuestWidescreenOwner::applyDrawArea(Core &core) {
  GpuState &gpu = core.game->gpu;
  const int right =
      math::widenedDrawAreaRight(gpu.s_da_x1, plan_.presentationExtent.width, facts_.nativeWidth);
  if (right == gpu.s_da_x1) {
    return false;
  }
  const int bottom = gpu.s_da_y1;
  // Re-issued through the framework's own GP0 decoder rather than by assigning the field, so the
  // widened rectangle is recorded exactly as a guest write of the same word would be recorded --
  // including in the `[env] E4 clip_br=` line. No guest word is involved: this is a GPU register.
  //
  // GP0, not GP1: libgpu builds the corner word as GP1(44h..47h) and the hardware decodes it as the
  // GP0 E3..E6 draw-environment commands, and the framework's `E4 clip_br` handler lives on the GP0
  // path. Sending this through `gpu_gp1` is a silent no-op -- it parses, matches no case, and the
  // rectangle is never widened, which reads as "the margins are black" rather than as a mistake.
  gpu_gp0(&core, math::drawAreaBottomRight(right, bottom));
  // What the decoder actually left behind, named. Reading it back rather than assuming the write
  // took is what separates "the rectangle is widened" from "the rectangle was widened and something
  // re-narrowed it before the next primitive", and the two look identical in a capture.
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
