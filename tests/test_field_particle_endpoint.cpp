#include "field_particle_endpoint.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using spyro::field_particle_endpoint::classify;
using spyro::field_particle_endpoint::Projected;

// 16:9 on Spyro 1: 684 drawn columns about a 342 centre against the guest's 256.
constexpr int32_t kWideRight = 684;
constexpr int32_t kWideDelta = 86;
constexpr uint32_t kDepth = 0x400u; // otDepth 32 with no bias

void require(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "field_particle_endpoint: " << what << '\n';
    std::exit(1);
  }
}

void testFourByThreeAnswersAgree() {
  // With no widening both answers are one answer, at both edges.
  for (int32_t sx = -4; sx <= 516; ++sx) {
    const auto a = classify({kDepth, sx, 100}, 0u, 0, 512);
    require(a.guest == a.drawn, "4:3 guest and drawn answers differ");
    require(a.guest == (sx > 0 && sx < 512), "4:3 edge is not the strict (0, 512) window");
  }
}

void testWideMarginIsDrawnButNotGuest() {
  // Drawn x 40 is in the left margin: guest x = 40 - 86 = -46, off the guest's screen.
  const auto left = classify({kDepth, 40, 100}, 0u, kWideDelta, kWideRight);
  require(left.drawn, "left-margin endpoint is not drawn");
  require(!left.guest, "left-margin endpoint wrote the widened answer into the guest byte");
  // Drawn x 640 is in the right margin: guest x = 554 >= 512.
  const auto right = classify({kDepth, 640, 100}, 0u, kWideDelta, kWideRight);
  require(right.drawn, "right-margin endpoint is not drawn");
  require(!right.guest, "right-margin endpoint wrote the widened answer into the guest byte");
}

void testWideEdgesMapToTheGuestEdges() {
  // The guest's strict edges 0 and 512 sit at drawn x 86 and 598.
  require(!classify({kDepth, 86, 100}, 0u, kWideDelta, kWideRight).guest, "guest x 0 is on-screen");
  require(classify({kDepth, 87, 100}, 0u, kWideDelta, kWideRight).guest, "guest x 1 is off-screen");
  require(classify({kDepth, 597, 100}, 0u, kWideDelta, kWideRight).guest, "guest x 511 is off");
  require(!classify({kDepth, 598, 100}, 0u, kWideDelta, kWideRight).guest, "guest x 512 is on");
  // And the drawn window's own strict edges.
  require(!classify({kDepth, 0, 100}, 0u, kWideDelta, kWideRight).drawn, "drawn x 0 is drawn");
  require(classify({kDepth, 683, 100}, 0u, kWideDelta, kWideRight).drawn, "drawn x 683 not drawn");
  require(!classify({kDepth, 684, 100}, 0u, kWideDelta, kWideRight).drawn, "drawn x 684 is drawn");
}

void testDepthAndRowRejectBoth() {
  // Negative cases: a depth or row failure rejects both answers, whatever the x.
  const Projected centre{kDepth, 342, 100};
  require(!classify({0u, 342, 100}, 0u, kWideDelta, kWideRight).drawn, "sz 0 drawn");
  require(!classify({0x2000u, 342, 100}, 0u, kWideDelta, kWideRight).guest, "sz 0x2000 guest");
  require(!classify({kDepth, 342, 0}, 0u, kWideDelta, kWideRight).drawn, "row 0 drawn");
  require(!classify({kDepth, 342, 256}, 0u, kWideDelta, kWideRight).guest, "row 256 guest");
  // otDepth = (0x60 >> 5) - 0 = 3 passes; a bias of 1 leaves 2, which fails.
  require(classify({0x60u, 342, 100}, 0u, kWideDelta, kWideRight).drawn, "otDepth 3 rejected");
  const auto biased = classify({0x60u, 342, 100}, 1u, kWideDelta, kWideRight);
  require(!biased.drawn && !biased.guest, "otDepth 2 accepted");
  require(biased.otDepth == 2, "otDepth is not sz>>5 minus the bias");
  const auto both = classify(centre, 0u, kWideDelta, kWideRight);
  require(both.guest && both.drawn, "the shared centre is not on-screen in both windows");
}

} // namespace

int main() {
  testFourByThreeAnswersAgree();
  testWideMarginIsDrawnButNotGuest();
  testWideEdgesMapToTheGuestEdges();
  testDepthAndRowRejectBoth();
  std::cout << "field_particle_endpoint: ok\n";
  return 0;
}
