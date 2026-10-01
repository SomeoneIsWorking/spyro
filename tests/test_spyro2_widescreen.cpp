// Spyro 2's horizontal widening arithmetic, and the two guest addresses the owner binds.
//
// The arithmetic is the whole of the visible change, so it is pinned here rather than discovered in
// a picture: the widened centre is half the plan's projection extent, the widened clipping
// rectangle reaches the plan's last column, and NEITHER ever narrows a rectangle the guest set for
// itself. The address assertions are here for the same reason `test_boot_prefix_facts` exists: a
// wrong guest address must fail in a test with no disc attached, not only in a route.
#include "spyro2_widescreen.h"
#include "spyro2_widescreen_math.h"

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
  using spyro2::ProjectionSite;
  using spyro2::WidescreenOwner;
  using spyro2::widescreen_math::drawAreaBottomRight;
  using spyro2::widescreen_math::drawAreaTopLeft;
  using spyro2::widescreen_math::guestWindowLeft;
  using spyro2::widescreen_math::guestWindowRight;
  using spyro2::widescreen_math::kGuestOfx;
  using spyro2::widescreen_math::kNativeHeight;
  using spyro2::widescreen_math::kNativeWidth;
  using spyro2::widescreen_math::widenedCenterX;
  using spyro2::widescreen_math::widenedDrawAreaRight;

  // THE AUTHORED EXTENT, from the display bootstrap's own geometry-init leaf
  // (80011D34 `addiu $a0,$zero,0x100`, 80011D3C `addiu $a1,$zero,0x78`,
  //  80011D44 `addiu $a0,$zero,0x155`): OFX 256 for a 512-dot display mode.
  expect(kGuestOfx == 256, "the guest's own OFX is 256");
  expect(kNativeWidth == 512, "the authored window is 512 wide");
  expect(kNativeHeight == 240, "the authored window is 240 high");
  expect(widenedCenterX(kNativeWidth) == kGuestOfx, "the 4:3 centre is the guest's own OFX");

  // POSITIVE — 512 -> 684 at 16:9, which is what `guest_wide_extent_width` resolves for a
  // 512-wide NTSC projection. The centre is half of it and the clip edge its last column.
  expect(widenedCenterX(684) == 342, "the widened 16:9 centre is 342");
  expect(widenedDrawAreaRight(511, 684) == 683, "the widened clip edge is 683");
  expect(widenedDrawAreaRight(511, 892) == 891, "21:9 widens further still");

  // WHERE THE GUEST'S OWN 512 COLUMNS LAND under a moved centre — the expectation the capture is
  // read against. At 4:3 they are 0..511 and the picture is the authored one; at 16:9 the centre is
  // 342 and the same 512 columns are 86..597, which is the measured capture's ink band. The margin
  // widths follow from it, so they are stated as numbers here rather than left to the reader.
  expect(guestWindowLeft(512) == 0 && guestWindowRight(512) == 511,
         "4:3 shows the authored window");
  expect(guestWindowLeft(684) == 86 && guestWindowRight(684) == 597,
         "16:9 shifts it by the margin");
  expect(guestWindowLeft(892) == 190 && guestWindowRight(892) == 701,
         "21:9 shifts it further still");
  // The widened clip edge must be OUTSIDE the guest's window or the guest's own last column is
  // clipped away by the very rectangle meant to reveal the extra columns. This is the one ordering
  // that matters between the two numbers above.
  expect(widenedDrawAreaRight(511, 684) > guestWindowRight(684),
         "the widened clip edge reaches past the guest's own window");

  // NEGATIVE — a plan that is not a widening leaves the guest's own rectangle exactly as it was.
  // This is the case that would silently crop the picture if the rule were written as an
  // unconditional assignment.
  expect(widenedDrawAreaRight(511, 512) == 511, "4:3 leaves the guest's clip edge alone");
  expect(widenedDrawAreaRight(511, 320) == 511, "a narrower plan does not narrow the clip edge");

  // NEVER NARROWS — a guest that already drew wider than the plan keeps its own edge. Retail's
  // draw area is the floor, not the target.
  expect(widenedDrawAreaRight(900, 684) == 900, "a wider guest rectangle is left alone");

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
  // names are asserted to carry the address they claim.
  expect(std::string_view(WidescreenOwner::siteName(ProjectionSite::SetGeomOffsetLeaf))
                 .find("0x80057AF8") != std::string_view::npos,
         "SetGeomOffset names 0x80057AF8");
  expect(std::string_view(WidescreenOwner::siteName(ProjectionSite::SetGeomScreenLeaf))
                 .find("0x80057AE8") != std::string_view::npos,
         "SetGeomScreen names 0x80057AE8");

  // The four inline restatements the owner does NOT intercept, stated so the count is a number
  // rather than a claim in a comment.
  expect(sizeof(spyro2::kInlineProjectionPublications) /
                 sizeof(spyro2::kInlineProjectionPublications[0]) ==
             4,
         "four inline mtc2 publication sites are named");

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("spyro2 widescreen arithmetic: all checks passed\n");
  return 0;
}
