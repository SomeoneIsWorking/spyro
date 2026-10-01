#include "spyro2_widescreen.h"

#include "core.h"
#include "game.h"
#include "gpu_native_internal.h"
#include "gpu_vk.h"
#include "guest_widescreen_projection.h"
#include "mods.h"
#include "native_execution.h"
#include "proj_params.h"
#include "spyro2_widescreen_math.h"
using spyro2::widescreen_math::kGuestOfx;
#include "spyro_context.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro2 {
namespace {

// ── THE TWO MEASURED PUBLICATION SITES
// ───────────────────────────────────────────────────────────── Both are `mtc2 rt, CR` — the GTE's
// move-to-control-register form — decoded from the provisioned executable. Capstone reports them
// UNKNOWN, and the reason is worth stating because it produces a clean zero when scanned the
// ordinary way: the GTE register number is in bits 15-11, not in bits 4-0 the way every ordinary
// COP2 form puts it. Every site in this file was found by reading bits 15-11, and each is quoted
// with its neighbours so it can be re-derived rather than trusted.

// 80057AE8  48c4d000  mtc2 $a0, $26          ; SetGeomScreen(h): CR26 = h
// 80057AEC  03e00008  jr   $ra
constexpr std::uint32_t kSetGeomScreenLeaf = 0x80057AE8u;

// 80057AF8  00042400  sll  $a0, $a0, 16
// 80057AFC  00052c00  sll  $a1, $a1, 16
// 80057B00  48c4c000  mtc2 $a0, $24          ; SetGeomOffset(x, y): CR24 = x<<16, CR25 = y<<16
// 80057B04  48c5c800  mtc2 $a1, $25
// 80057B08  03e00008  jr   $ra
constexpr std::uint32_t kSetGeomOffsetLeaf = 0x80057AF8u;

WidescreenOwner &ownerOf(Core &core) {
  GuestProjectionOwner *hook = spyro_context(core).projectionHook;
  if (hook == nullptr) {
    // Both overrides are installed by the same call that publishes this pointer, so a null here
    // means the publication did not happen. Continuing would draw a 4:3 projection silently.
    lucent::error("wide", "Spyro 2 projection leaf reached with no projection owner published");
    std::abort();
  }
  return *static_cast<WidescreenOwner *>(hook);
}

// ── THE TWO RETAIL BODIES
// ──────────────────────────────────────────────────────────────────────────── Each runs retail's
// own effect FIRST and asks the owner afterwards, so the widened value is what the rest of the
// frame sees even when the guest restates its projection through a leaf.

// SetGeomScreen(h). Retail moves $a0 into CR26 and returns. The framework's own `PlatformHlePlan`
// handler for this address does the same and additionally records the projection the guest stated;
// this body is that handler with the plan applied afterwards.
void setGeomScreen(Core *core) {
  libgte_set_geom_screen(core, static_cast<std::int32_t>(core->r[4]));
  ownerOf(*core).published(*core, ProjectionSite::SetGeomScreenLeaf);
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
  ownerOf(*core).published(*core, ProjectionSite::SetGeomOffsetLeaf);
}

} // namespace

PresentationAspect WidescreenOwner::presentationAspect(const Core &core) const {
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
    lucent::error("wide", "Spyro 2 received invalid aspect selector {}", core.game->mods.aspect);
    std::abort();
  }
}

void WidescreenOwner::registerProjectionOverrides(Core &core) {
  spyro_context(core).projectionHook = this;

  // Both leaves are ALSO in `PlatformHlePlan`. That is not a conflict: the framework's own dispatch
  // contract consults a title's image-scoped override BEFORE any host-service table, so these two
  // replace the stock handlers rather than queueing behind them.
  spyro::installNativeOverride(core, kSetGeomScreenLeaf, "spyro2-set-geom-screen", setGeomScreen);
  spyro::installNativeOverride(core, kSetGeomOffsetLeaf, "spyro2-set-geom-offset", setGeomOffset);

  lucent::info("wide",
               "Spyro 2 projection bound at 2 measured library leaves plus the 4 measured inline "
               "mtc2 restatements carried by the per-field re-assertion (not intercepted: they are "
               "mid-block and a native override resumes at $ra); {}",
               siteName(ProjectionSite::SetGeomOffsetLeaf));
}

namespace {
// CR24 and CR25 are OFX and OFY, SIGNED 16-bit fields in the upper half of their control register.
std::int32_t gte_offset_x() {
  return static_cast<std::int32_t>(gte_read_ctrl(24) >> 16);
}
std::int32_t gte_offset_y() {
  return static_cast<std::int32_t>(gte_read_ctrl(25) >> 16);
}
} // namespace

void WidescreenOwner::published(Core &core, ProjectionSite site) {
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
               gte_offset_x(),
               gte_offset_y(),
               widescreen_math::widenedCenterX(plan_.projectionExtent.width));
  applyProjection(core);
}

void WidescreenOwner::onProjectionPublished(Core &core) {
  published(core, ProjectionSite::SetGeomOffsetLeaf);
}

void WidescreenOwner::onFieldDelivered(Core &core) {
  onField(core, /*startEdge=*/false);
}

void WidescreenOwner::onField(Core &core, bool startEdge) {
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

void WidescreenOwner::onFrameTail(Core &core) {
  if (!latched_ || !plan_.widescreen()) {
    return;
  }
  if (applyDrawArea(core)) {
    ++widenedAreas_;
  }
}

bool WidescreenOwner::latch(Core &core) {
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

void WidescreenOwner::applyProjection(Core &core) {
  // What the guest itself left in CR24 at this point, named rather than overwritten silently: if
  // this reads the 4:3 centre the guest restated its projection during the frame that has just
  // been drawn, and the re-assertion is what puts it back — which is the whole reason this owner
  // runs at the step boundary as well as at the publication.
  const auto centerX = widescreen_math::widenedCenterX(plan_.projectionExtent.width);
  // Only the horizontal centre moves. The vertical centre and the projection distance stay at the
  // values the guest itself published, so the vertical field of view is the console's and nothing
  // in the picture is scaled.
  const auto centerY = static_cast<std::int32_t>(gte_read_ctrl(25) >> 16);
  libgte_set_geom_offset(&core, centerX, centerY);
}

bool WidescreenOwner::applyDrawArea(Core &core) {
  GpuState &gpu = core.game->gpu;
  const int right =
      widescreen_math::widenedDrawAreaRight(gpu.s_da_x1, plan_.presentationExtent.width);
  if (right == gpu.s_da_x1) {
    return false;
  }
  const int bottom = gpu.s_da_y1;
  // Re-issued through the framework's own GP0 decoder rather than by assigning the field, so the
  // widened rectangle is recorded exactly as a guest write of the same word would be recorded —
  // including in the `[env] E4 clip_br=` line. No guest word is involved: this is a GPU register.
  //
  // GP0, not GP1: libgpu builds the corner word as GP1(44h..47h) and the hardware decodes it as the
  // GP0 E3..E6 draw-environment commands, and the framework's `E4 clip_br` handler lives on the GP0
  // path. Sending this through `gpu_gp1` is a silent no-op — it parses, matches no case, and the
  // rectangle is never widened, which reads as "the margins are black" rather than as a mistake.
  gpu_gp0(&core, widescreen_math::drawAreaBottomRight(right, bottom));
  // What the decoder actually left behind, named. Reading it back rather than assuming the write
  // took is what separates "the rectangle is widened" from "the rectangle was widened and something
  // re-narrowed it before the next primitive", and the two look identical in a capture.
  lucent::debug("wide",
                "widened draw area right {} -> {} (bottom {}); register now ({}, {})",
                right - (plan_.presentationExtent.width - plan_.nativeExtent.width) / 2,
                right,
                bottom,
                gpu.s_da_x1,
                gpu.s_da_y1);
  return true;
}

void WidescreenOwner::report() const {
  lucent::info("wide",
               "Spyro 2 widescreen owner latched from the GUEST'S OWN display mode {}x{} "
               "(guest published OFX {} OFY {}): "
               "aspect={} presentation {}x{} (margin {}px), projection {}x{} (margin {}px, centre "
               "X {}), guest draw width {} -> {}. {}",
               plan_.nativeExtent.width,
               plan_.nativeExtent.height,
               gte_offset_x(),
               gte_offset_y(),
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

const char *WidescreenOwner::siteName(ProjectionSite site) {
  switch (site) {
  case ProjectionSite::SetGeomOffsetLeaf:
    return "0x80057AF8 SetGeomOffset";
  case ProjectionSite::SetGeomScreenLeaf:
    return "0x80057AE8 SetGeomScreen";
  }
  return "?";
}

const char *WidescreenOwner::aspectName(PresentationAspect aspect) {
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

} // namespace spyro2
