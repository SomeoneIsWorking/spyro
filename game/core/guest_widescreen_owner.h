// guest_widescreen_owner.h — ONE widening decision for a title that presents its own guest VRAM,
// and every place that guest restates it.
//
// WHY IT IS A GUEST-PROJECTION OWNER AND NOT A NATIVE RENDERER. The titles that use it run on the
// GTE path (`SpyroRuntime::renderCapabilities().defaultPath == RenderPath::Gte`, `nativeRenderPath
// == false`): every presented field is guest VRAM produced by the guest's own GTE and GP0 stream
// through Lightrec. There is no native producer to re-centre, so the widening has to happen where
// it happens one level down -- in the projection the guest itself projects with.
//
// THE FRAMEWORK CONTRACT THIS USES, and nothing else:
//   * `GuestWidescreenProjection::presentationAspect` is the title's own answer to the configured
//     aspect. Before it exists the framework resolves `requested = Standard4x3` and every plan is
//     4:3 whatever the settings say.
//   * `gpu_vk_latch_guest_projection` resolves that answer against the guest's OWN published extent
//     and publishes the plan the host presenter consumes. The presenter then presents
//     `plan.presentationExtent.width` columns, and `wide_2d_layout` re-centres the guest's 2D layer
//     by the same margin, which is what keeps the HUD unstretched.
//
// WHAT IS WIDENED, AND WHY IT IS ONLY THAT:
//   * the HORIZONTAL CENTRE, OFX. The vertical centre (OFY) and the projection distance (H) are
//     left at retail's values, so the vertical field of view is bit-for-bit the console's and only
//     the horizontal one grows. Nothing is scaled, so nothing is stretched.
//   * the GPU's DRAWING RECTANGLE, whose right edge retail clamps to the authored window. Without
//     it the extra geometry is rasterised and then clipped away, and the margins present black.
//
// WHAT IS NEVER TOUCHED: a guest word. Every scalar here is a GTE control register or a GPU
// register re-issued through the framework's own decoder. `H` in particular is a coupled scalar in
// this lineage's engine (it divides the guest's own perspective maths), and widening it would move
// guest-visible geometry for no presentation gain -- so the plan never writes it.
//
// WHAT IS A TITLE FACT AND WHAT IS NOT. The library leaves the guest reaches its projection
// through, its authored window, and the inline `mtc2` sites it restates CR24/CR25 at are measured
// per image and arrive in `GuestWidescreenFacts`. Everything else -- when to assert, what to
// assert, and the arithmetic -- is this one implementation, so two titles of the same engine
// family cannot drift into two different widenings.
#pragma once

#include "field_owner.h"
#include "guest_projection_owner.h"
#include "guest_widescreen_projection.h"

#include <cstddef>
#include <cstdint>

class Core;

namespace spyro {

// WHERE a projection publication came from. Named rather than a bare address so a log line reads as
// a statement about the measured sites instead of as hex the reader has to look up.
enum class ProjectionSite : std::uint8_t {
  SetGeomOffsetLeaf = 0,
  SetGeomScreenLeaf = 1,
};

// A title's own measured projection facts. Every field here is read out of that title's image; a
// field that is zero says "this image has no such site", which is a measurement and not a gap.
struct GuestWidescreenFacts {
  const char *titleName = "";
  // The override names the framework's differential and its logs identify each bound leaf by. They
  // are distinct strings because two leaves sharing one name make a differential line ambiguous
  // about which body produced it.
  const char *setGeomOffsetOverrideName = "";
  const char *setGeomScreenOverrideName = "";

  // The libgpu leaves the guest reaches its projection through, reached by `jal` so a native
  // override resumes at `$ra` correctly. Zero when this image reaches no such leaf.
  std::uint32_t setGeomOffsetLeaf = 0;
  std::uint32_t setGeomScreenLeaf = 0;

  // How each bound leaf is named in a log line, so the message states a site rather than a number.
  const char *setGeomOffsetSiteName = "";
  const char *setGeomScreenSiteName = "";

  // The authored window this title's own display bootstrap states through the leaves above. Spyro 2
  // and Spyro 3 both state OFX = 256 for the 512-dot display mode, so both native windows are 512
  // wide; they are still per-title facts because the widening is stated as a multiple of them.
  int guestOffsetX = 0;
  int nativeWidth = 0;
  int nativeHeight = 0;

  // The sites where this image restates CR24/CR25 INLINE, as `mtc2 rt, CRn` rather than through a
  // library leaf.
  //
  // They are deliberately NOT claimed as overrides, and the reason is the framework's own override
  // contract: a native override resumes at `$ra`, captured when the override was entered, so it is
  // only correct at an address the guest REACHED BY `jal`. An override planted on a mid-block
  // `mtc2` would resume at the enclosing function's saved return address. These are named here so
  // the gap is stated rather than discovered as black margins: the per-field re-assertion covers
  // them instead.
  const std::uint32_t *inlineProjectionPublications = nullptr;
  std::size_t inlineProjectionPublicationCount = 0;
};

class GuestWidescreenOwner final : public GuestWidescreenProjection,
                                   public GuestProjectionOwner,
                                   public FieldObserver,
                                   public FrameTailObserver {
public:
  explicit GuestWidescreenOwner(const GuestWidescreenFacts &facts) : facts_(facts) {}

  // This Core's owner, published by `registerProjectionOverrides`. Native overrides are plain
  // function pointers, so they reach the owner through the context rather than a back-pointer.
  //
  // NULLABLE, and that is the lifecycle, not a hedge. A Core whose leaves are not bound yet has no
  // owner, and a Core that is bound publishes one before any consumer is installed. Consumers must
  // therefore treat "no owner" as the same thing an unlatched owner already means: this title is
  // not widening. Consumers that CANNOT widen without an owner ask `require`.
  static GuestWidescreenOwner *of(Core &core) noexcept;

  // The same owner, for the bound projection leaves themselves. Those two functions are installed
  // BY the publishing call, so a null there would mean a leaf was bound without its owner — which
  // is a real defect and aborts rather than silently presenting 4:3.
  static GuestWidescreenOwner &require(Core &core);

  const GuestWidescreenFacts &facts() const {
    return facts_;
  }

  PresentationAspect presentationAspect(const Core &core) const override;

  // Bind this Core's measured library publication sites. Each runs the retail effect FIRST and
  // applies the plan after it, so a guest that restates its projection through a leaf still ends
  // the frame widened rather than reverting to 4:3 for the rest of it.
  void registerProjectionOverrides(Core &core);

  // Retail's own write has just happened at `site`. Latch the plan on the first publication whose
  // display mode the guest has already published, and re-assert the widened horizontal centre.
  void published(Core &core, ProjectionSite site);

  // The two step boundaries, and they are DIFFERENT jobs, which is measured rather than assumed.
  //
  //   onField — once per delivered field, before the guest resumes drawing. The widened
  //   horizontal centre is asserted HERE. Measured on Spyro 2: at the frame tail CR24 already reads
  //   the widened centre on 2299 of 2513 steps, which says the guest does NOT restate it mid-frame
  //   -- and that is precisely why asserting it at the tail is too LATE. The tail assert widens the
  //   field AFTER the frame that has just been drawn, so the picture stays at the 4:3 centre: a
  //   capture taken at the field boundary matched the 4:3 reference at 13.6% and no shift at all,
  //   where the field-boundary assert matched it at 99.96% at exactly +86 px.
  //
  //   onFrameTail — once per step, after the guest's last command and before the queue rasterises.
  //   The GPU drawing rectangle is widened HERE, and only here: the guest MEASURED reissues GP1
  //   E3/E4 twice per frame at x1 = 511, so a rectangle widened before the guest resumes is
  //   re-narrowed before a single primitive is drawn. Rasterisation happens after this point and
  //   before the next one, which is the only interval in which a widened rectangle can affect a
  //   pixel.
  void onFrameTail(Core &core) override;

  // `GuestProjectionOwner`. The projection half; the drawing rectangle is not guest projection and
  // is deliberately NOT routed through this interface.
  void onProjectionPublished(Core &core) override;

  // `spyro::FieldObserver` — the per-field boundary.
  //
  // `startEdge` is ignored ON PURPOSE and that is the whole point: this owner has no boot edge to
  // distinguish, and a guard of the form `if (!startEdge) return;` — the natural reading of the
  // flag as "skip the first field" — silently disables the entire widening while every other line
  // still compiles and every counter still reads plausible. The field owner calls this once per
  // delivered field, so the widened centre is in CR24 before the guest resumes drawing.
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

  // The columns a wider presentation shows past each side of this title's authored window, or 0
  // while the plan has not latched or does not widen. Every horizontal cull in the title asks this
  // one question rather than re-deriving the margin from the plan.
  [[nodiscard]] int horizontalMargin() const;

private:
  [[nodiscard]] const char *siteName(ProjectionSite site) const;
  bool latch(Core &core);
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
