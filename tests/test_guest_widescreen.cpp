// The shared horizontal widening arithmetic, and each title's measured sites against its own bytes.
//
// The arithmetic is the whole of the visible change, so it is pinned here rather than discovered in
// a picture: the widened centre is half the plan's projection extent, the widened clipping
// rectangle reaches the plan's last column, and NEITHER ever narrows a rectangle the guest set for
// itself. The per-title address assertions are here for the same reason `test_boot_prefix_facts`
// exists: a wrong guest address must fail in a test with no disc attached, not only in a route --
// and now that two images bind the same owner, a title that silently inherited the other's leaf
// would widen the wrong projection.
#include "guest_widescreen_math.h"
#include "guest_widescreen_owner.h"
#include "spyro2_widescreen_facts.h"
#include "spyro3_widescreen_facts.h"

#include <cstdint>
#include <cstdio>
#include <string_view>

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
  using spyro::ProjectionSite;
  using spyro::guest_widescreen_math::drawAreaBottomRight;
  using spyro::guest_widescreen_math::drawAreaTopLeft;
  using spyro::guest_widescreen_math::guestWindowLeft;
  using spyro::guest_widescreen_math::guestWindowRight;
  using spyro::guest_widescreen_math::widenedCenterX;
  using spyro::guest_widescreen_math::widenedDrawAreaRight;

  // THE AUTHORED EXTENT. Spyro 2's display bootstrap states it in its own geometry-init leaf
  // (80011D34 `addiu $a0,$zero,0x100`, 80011D3C `addiu $a1,$zero,0x78`,
  //  80011D44 `addiu $a0,$zero,0x155`); Spyro 3's states the same pair. Both facts are asserted
  // rather than assumed, because a title whose authored window differed would need a different
  // margin and the arithmetic below would then be wrong for it.
  for (const spyro::GuestWidescreenFacts *facts :
       {&spyro2::kWidescreenFacts, &spyro3::kWidescreenFacts}) {
    expect(facts->guestOffsetX == 256, "the guest's own OFX is 256");
    expect(facts->nativeWidth == 512, "the authored window is 512 wide");
    expect(facts->nativeHeight == 240, "the authored window is 240 high");
    expect(facts->nativeWidth == 2 * facts->guestOffsetX,
           "the authored window is twice its own centre");
    expect(widenedCenterX(facts->nativeWidth) == facts->guestOffsetX,
           "the 4:3 centre is the guest's own OFX");
  }

  // POSITIVE — 512 -> 684 at 16:9, which is what `guest_wide_extent_width` resolves for a
  // 512-wide NTSC projection. The centre is half of it and the clip edge its last column.
  expect(widenedCenterX(684) == 342, "the widened 16:9 centre is 342");
  expect(widenedDrawAreaRight(511, 684, 512) == 683, "the widened clip edge is 683");
  expect(widenedDrawAreaRight(511, 892, 512) == 891, "21:9 widens further still");

  // WHERE THE GUEST'S OWN 512 COLUMNS LAND under a moved centre — the expectation a capture is read
  // against. At 4:3 they are 0..511 and the picture is the authored one; at 16:9 the centre is 342
  // and the same 512 columns are 86..597. The margin widths follow from it, so they are stated as
  // numbers here rather than left to the reader.
  expect(guestWindowLeft(512, 256) == 0 && guestWindowRight(512, 256) == 511,
         "4:3 shows the authored window");
  expect(guestWindowLeft(684, 256) == 86 && guestWindowRight(684, 256) == 597,
         "16:9 shifts it by the margin");
  expect(guestWindowLeft(892, 256) == 190 && guestWindowRight(892, 256) == 701,
         "21:9 shifts it further still");
  // The widened clip edge must be OUTSIDE the guest's window or the guest's own last column is
  // clipped away by the very rectangle meant to reveal the extra columns. This is the one ordering
  // that matters between the two numbers above.
  expect(widenedDrawAreaRight(511, 684, 512) > guestWindowRight(684, 256),
         "the widened clip edge reaches past the guest's own window");

  // NEGATIVE — a plan that is not a widening leaves the guest's own rectangle exactly as it was.
  // This is the case that would silently crop the picture if the rule were written as an
  // unconditional assignment.
  expect(widenedDrawAreaRight(511, 512, 512) == 511, "4:3 leaves the guest's clip edge alone");
  expect(widenedDrawAreaRight(511, 320, 512) == 511,
         "a narrower plan does not narrow the clip edge");

  // NEVER NARROWS — a guest that already drew wider than the plan keeps its own edge. Retail's
  // draw area is the floor, not the target.
  expect(widenedDrawAreaRight(900, 684, 512) == 900, "a wider guest rectangle is left alone");

  // The two GP1 draw-area corner words, against the framework's own packing (X in bits 0..9, Y in
  // bits 10..18, command byte in bits 24..31). A swapped pair of axes here is a wrong PAGE of VRAM
  // rather than a wrong pixel, so it is pinned rather than left to the reader of the header.
  expect(drawAreaTopLeft(0, 0) == 0xE3000000u, "top-left at the origin");
  expect(drawAreaBottomRight(683, 239) == (0xE4000000u | (239u << 10) | 683u),
         "bottom-right packs both axes");
  expect((drawAreaBottomRight(683, 239) >> 24) == 0xE4u,
         "the bottom-right command byte survives packing");
  expect(((drawAreaBottomRight(683, 239) >> 10) & 0x1FFu) == 239u, "Y survives the pack");
  expect((drawAreaBottomRight(683, 239) & 0x3FFu) == 683u, "X survives the pack");

  // THE SITES. A named site is a claim about a guest address, so each is spelled out here and the
  // names are asserted to carry the address they claim. Spyro 2 and Spyro 3 are asserted
  // SEPARATELY: they share the owner and differ only in these facts, so a leaf one of them
  // inherited from the other would still satisfy a shared check.
  expect(std::string_view(spyro2::kWidescreenFacts.setGeomOffsetSiteName).find("0x80057AF8") !=
             std::string_view::npos,
         "Spyro 2 SetGeomOffset names 0x80057AF8");
  expect(std::string_view(spyro2::kWidescreenFacts.setGeomScreenSiteName).find("0x80057AE8") !=
             std::string_view::npos,
         "Spyro 2 SetGeomScreen names 0x80057AE8");
  expect(spyro2::kWidescreenFacts.setGeomOffsetLeaf == 0x80057AF8u &&
             spyro2::kWidescreenFacts.setGeomScreenLeaf == 0x80057AE8u,
         "Spyro 2 binds its own two leaves");
  expect(std::string_view(spyro3::kWidescreenFacts.setGeomOffsetSiteName).find("0x8005D35C") !=
             std::string_view::npos,
         "Spyro 3 SetGeomOffset names 0x8005D35C");
  expect(spyro3::kWidescreenFacts.setGeomOffsetLeaf == 0x8005D35Cu,
         "Spyro 3 binds its own SetGeomOffset leaf");
  // Spyro 3 claims NO SetGeomScreen site, and that is the measured shape of its image: its one
  // CR26 publication is a projection-distance write the widening never touches.
  expect(spyro3::kWidescreenFacts.setGeomScreenLeaf == 0, "Spyro 3 claims no SetGeomScreen leaf");

  // The inline restatements each owner does NOT intercept, stated so the counts are numbers rather
  // than claims in a comment.
  expect(spyro2::kWidescreenFacts.inlineProjectionPublicationCount == 4,
         "Spyro 2 names four inline mtc2 publication sites");
  expect(spyro3::kWidescreenFacts.inlineProjectionPublicationCount == 4,
         "Spyro 3 names four inline mtc2 publication sites");
  expect(std::string_view(spyro3::kWidescreenFacts.setGeomScreenSiteName).empty(),
         "an unbound site has no name to log");

  // The two titles must not share one leaf. They are different images and the whole point of the
  // facts table is that the shared owner reads none of them.
  expect(spyro2::kWidescreenFacts.setGeomOffsetLeaf != spyro3::kWidescreenFacts.setGeomOffsetLeaf,
         "the two titles bind different projection leaves");

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("guest widescreen arithmetic: all checks passed\n");
  return 0;
}
