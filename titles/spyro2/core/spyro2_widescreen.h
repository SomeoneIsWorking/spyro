// spyro2_widescreen.h — Spyro 2's ONE widening decision, and every place the guest restates it.
//
// WHY THIS IS A GUEST-PROJECTION OWNER AND NOT A NATIVE RENDERER. Spyro 2 runs on the GTE path
// (`SpyroRuntime::renderCapabilities().defaultPath == RenderPath::Gte`, `nativeRenderPath ==
// false`): every presented field is guest VRAM produced by the guest's own GTE and GP0 stream
// through Lightrec (`Spyro2Runtime::guestVramIsPicture` returns true and says so). There is no
// native producer to re-centre, so the widening has to happen where Spyro 1's happens one level
// down — in the projection the guest itself projects with.
//
// THE FRAMEWORK CONTRACT THIS USES, and nothing else:
//   * `GuestWidescreenProjection::presentationAspect` is the title's own answer to the configured
//     aspect. Before it exists the framework resolves `requested = Standard4x3` and every plan is
//     4:3 whatever the settings say — the measured root cause of CTR not widening, and the reason
//     this is a class rather than a line in the frame driver.
//   * `gpu_vk_latch_guest_projection` resolves that answer against the guest's OWN published extent
//     and publishes the plan the host presenter consumes. The presenter then presents
//     `plan.presentationExtent.width` columns, and `wide_2d_layout` re-centres the guest's 2D layer
//     by the same margin, which is what keeps the HUD unstretched.
//
// WHAT IS WIDENED, AND WHY IT IS ONLY THAT:
//   * the HORIZONTAL CENTRE, OFX. `Spyro2's` vertical centre (OFY) and projection distance (H) are
//     left at retail's values, so the vertical field of view is bit-for-bit the console's and only
//     the horizontal one grows. Nothing is scaled, so nothing is stretched.
//   * the GPU's DRAWING RECTANGLE, whose right edge retail clamps to the authored window. Without
//     it the extra geometry is rasterised and then clipped away, and the margins present black.
//
// WHAT IS NEVER TOUCHED: a guest word. Every scalar here is a GTE control register or a GPU
// register re-issued through the framework's own decoder. `H` in particular is a coupled scalar in
// this lineage's engine (it divides the guest's own perspective maths), and widening it would move
// guest-visible geometry for no presentation gain — so the plan never writes it.
#pragma once

#include "field_owner.h"
#include "guest_projection_owner.h"
#include "guest_widescreen_projection.h"

#include <cstdint>

class Core;

namespace spyro2 {

// WHERE a projection publication came from. Named rather than a bare address so a log line reads as
// a statement about the two measured sites instead of as hex the reader has to look up.
enum class ProjectionSite : std::uint8_t {
  SetGeomOffsetLeaf = 0, // 0x80057AF8 — libgte SetGeomOffset, reached by `jal`
  SetGeomScreenLeaf = 1, // 0x80057AE8 — libgte SetGeomScreen, reached by `jal`
};

// WHAT THE IMAGE SAYS ELSEWHERE, and why it is not intercepted.
//
// SCUS_944.25 restates CR24/CR25 inline as well, at four sites that are NOT library leaves:
//
//   8004726C  sll $at,$at,16 ; 80047270  sll $v0,$v0,16
//   80047274  mtc2 $at,$24   ; 80047278  mtc2 $v0,$25
//   80047EA8  lui $at,0x100  ; 80047EAC  mtc2 $at,$24
//   80047EB0  lui $at,0x78   ; 80047EB4  mtc2 $at,$25
//
// (0x01000000 and 0x00780000 are 256.0 and 120.0 in the 16.16 the control registers hold — the
// same pair the display bootstrap states through the two leaves.) They are deliberately left to the
// per-field re-assertion below rather than claimed as overrides, and the reason is the framework's
// own override contract: a native override resumes at `$ra`, captured when the override was
// entered, so it is only correct at an address the guest REACHED BY `jal`. An override planted on a
// mid-block `mtc2` would resume at the enclosing function's saved return address. These two sites
// are named here so the gap is stated rather than discovered as black margins.
inline constexpr std::uint32_t kInlineProjectionPublications[] = {
    0x80047274u,
    0x80047278u,
    0x80047EACu,
    0x80047EB4u,
};

class WidescreenOwner final : public GuestWidescreenProjection,
                              public GuestProjectionOwner,
                              public spyro::FieldObserver,
                              public spyro::FrameTailObserver {
public:
  PresentationAspect presentationAspect(const Core &core) const override;

  // Bind the two measured library publication sites for this Core. Each runs the retail effect
  // FIRST and applies the plan after it, so a guest that restates its projection through a leaf
  // still ends the frame widened rather than reverting to 4:3 for the rest of it.
  void registerProjectionOverrides(Core &core);

  // Retail's own write has just happened at `site`. Latch the plan on the first publication whose
  // display mode the guest has already published, and re-assert the widened horizontal centre.
  void published(Core &core, ProjectionSite site);

  // The two step boundaries, and they are DIFFERENT jobs, which is measured rather than assumed.
  //
  //   onField — once per delivered field, before the guest resumes drawing. The widened
  //   horizontal centre is asserted HERE. Measured: at the frame tail CR24 already reads the
  //   widened centre on 2299 of 2513 steps, which says the guest does NOT restate it mid-frame --
  //   and that is precisely why asserting it at the tail is too LATE. The tail assert widens the
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

  static const char *siteName(ProjectionSite site);
  static const char *aspectName(PresentationAspect aspect);

private:
  bool latch(Core &core);
  void applyProjection(Core &core);
  [[nodiscard]] bool applyDrawArea(Core &core);
  void report() const;

  GuestProjectionPlan plan_{};
  std::uint64_t publications_ = 0;
  std::uint64_t widenedSteps_ = 0;
  std::uint64_t widenedAreas_ = 0;
  bool latched_ = false;
};

} // namespace spyro2
