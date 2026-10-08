// One widening decision for a title; only OFX and the GPU draw-area right edge move, and on the
// Record path neither does because the canvas holds the margins. Nothing is stretched.
#pragma once

#include "field_owner.h"
#include "guest_projection_owner.h"
#include "guest_widescreen_projection.h"
#include "ui_anchor.h"

#include <cstddef>
#include <cstdint>

class Core;

namespace spyro {

// Where a projection publication came from, named so a log line reads as a statement about the
// measured sites.
enum class ProjectionSite : std::uint8_t {
  SetGeomOffsetLeaf = 0,
  SetGeomScreenLeaf = 1,
};

// A title's own measured projection facts; a zero field means this image has no such site.
struct GuestWidescreenFacts {
  const char *titleName = "";
  // Distinct strings because two leaves sharing one override name make a differential line
  // ambiguous about which body produced it.
  const char *setGeomOffsetOverrideName = "";
  const char *setGeomScreenOverrideName = "";

  // The libgpu leaves the guest reaches its projection through, bound by `jal` so a native
  // override resumes at `$ra`. Zero when this image reaches no such leaf.
  std::uint32_t setGeomOffsetLeaf = 0;
  std::uint32_t setGeomScreenLeaf = 0;

  // How each bound leaf is named in a log line, so the message states a site rather than a number.
  const char *setGeomOffsetSiteName = "";
  const char *setGeomScreenSiteName = "";

  // The authored window this title's display bootstrap states through the leaves above; Spyro 2 and
  // 3 both state OFX = 256 for the 512-dot mode.
  int guestOffsetX = 0;
  int nativeWidth = 0;
  int nativeHeight = 0;

  // The sites where this image restates CR24/CR25 INLINE as `mtc2 rt, CRn`. A native override
  // resumes at the `$ra` captured when it was entered, so it is only correct at an address the
  // guest reached by `jal`; the per-field re-assertion covers these instead.
  const std::uint32_t *inlineProjectionPublications = nullptr;
  std::size_t inlineProjectionPublicationCount = 0;
};

class GuestWidescreenOwner final : public GuestWidescreenProjection,
                                   public spyro::GuestProjectionOwner,
                                   public FieldObserver,
                                   public FrameTailObserver {
public:
  explicit GuestWidescreenOwner(const GuestWidescreenFacts &facts) : facts_(facts) {}

  // This Core's owner, published by `registerProjectionOverrides`; overrides are plain function
  // pointers, so they reach the owner through the context rather than a back-pointer. Null means
  // this title is not widening; consumers that cannot widen without one ask `require`.
  static GuestWidescreenOwner *of(Core &core) noexcept;

  // For the bound projection leaves themselves, which are installed by the publishing call; a null
  // there would mean a leaf was bound without its owner.
  static GuestWidescreenOwner &require(Core &core);

  const GuestWidescreenFacts &facts() const {
    return facts_;
  }

  // Each runs the retail effect FIRST and applies the plan after it, so a guest that restates its
  // projection through a leaf still ends the frame widened.
  void registerProjectionOverrides(Core &core);

  // Retail's own write has just happened at `site`. Latch the plan on the first publication whose
  // display mode the guest has already published, and re-assert the widened horizontal centre.
  void published(Core &core, ProjectionSite site);

  // The two step boundaries, and they are different jobs:
  //
  //   onField — per delivered field, before the guest resumes drawing. The widened horizontal
  //   centre is asserted HERE: the guest does not restate CR24 mid-frame, so a tail assert widens
  //   only the field AFTER the frame just drawn and the picture stays at the 4:3 centre.
  //
  //   onFrameTail — per step, after the guest's last command and before the queue rasterises. The
  //   GPU draw area is widened HERE and only here: the guest reissues GP1 E3/E4 twice per frame
  //   at x1 = 511, so a rectangle widened before the guest resumes is re-narrowed before a single
  //   primitive is drawn. Rasterisation is the only interval in which a widened rectangle can
  //   affect a pixel.
  void onFrameTail(Core &core) override;

  // `spyro::GuestProjectionOwner`; the drawing rectangle is not guest projection.
  void onProjectionPublished(Core &core) override;

  // `spyro::FieldObserver`. `startEdge` is ignored: this owner has no boot edge to distinguish, and
  // a `if (!startEdge) return;` guard would silently disable the whole widening.
  void onField(Core &core, bool startEdge) override;

  [[nodiscard]] bool latched() const {
    return latched_;
  }
  [[nodiscard]] const GuestProjectionPlan &plan() const {
    return plan_;
  }
  [[nodiscard]] std::uint64_t publications() const {
    return publications_;
  }
  [[nodiscard]] std::uint64_t widenedFields() const {
    return widenedSteps_;
  }
  [[nodiscard]] std::uint64_t widenedDrawAreas() const {
    return widenedAreas_;
  }

  static const char *aspectName(PresentationAspect aspect);

  // The columns a wider presentation shows past each side of the authored window, or 0 while the
  // plan has not latched or does not widen.
  [[nodiscard]] int horizontalMargin() const;

  // The frame `spyro::ui_anchor` relates for this title; the one adapter between the widening plan
  // and the anchoring policy. A Core with no owner, or one that has not latched or does not widen,
  // returns equal widths, which makes every anchor correction zero.
  [[nodiscard]] ui_anchor::Frame uiFrame() const;

private:
  [[nodiscard]] const char *siteName(ProjectionSite site) const;
  bool latch(Core &core);
  // Gte moves the horizontal centre; Record keeps the retail one and widens the canvas instead.
  [[nodiscard]] bool movesCentre() const;
  void applyProjection(Core &core);
  [[nodiscard]] bool applyDrawArea(Core &core);
  void report() const;

  GuestWidescreenFacts facts_;
  GuestProjectionPlan plan_{};
  std::uint64_t publications_ = 0;
  std::uint64_t widenedSteps_ = 0;
  std::uint64_t widenedAreas_ = 0;
  bool latched_ = false;
};

} // namespace spyro
