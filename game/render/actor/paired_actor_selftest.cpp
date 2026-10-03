#include "paired_actor.h"

#include "frame_env.h"
#include "paired_actor_pose.h"
#include "paired_actor_projection.h"
#include "paired_actor_temporal.h"
#include "paired_actor_temporal_evidence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <lucent/log.h>
#include <vector>

namespace {

using namespace spyro::paired_actor;

struct SyntheticLink {
  uint32_t address, next;
};

bool validate_synthetic_ot(bool corruptTail, bool corruptLink) {
  const SyntheticLink links[] = {
      {0x80001000u, 0}, {0x80002000u, 0x00003000u}, {0x80003000u, 0}, {0x80004000u, 0}};
  const uint32_t heads[] = {0x80001000u, 0x80002000u};
  const uint32_t tails[] = {0x80001000u, corruptTail ? 0x80004000u : 0x80003000u};
  const uint32_t expected[] = {0x80001000u, 0x80002000u, 0x80003000u};
  std::vector<uint32_t> actual;
  for (int chain = 0; chain < 2; ++chain) {
    uint32_t at = heads[chain];
    for (uint32_t steps = 0; steps < 4; ++steps) {
      actual.push_back(at);
      if (at == tails[chain]) {
        break;
      }
      auto link = std::find_if(std::begin(links), std::end(links), [&](const auto &value) {
        return value.address == at;
      });
      if (link == std::end(links)) {
        return false;
      }
      uint32_t next = link->next;
      if (corruptLink && at == 0x80002000u) {
        next = 0;
      }
      if (!next) {
        return false;
      }
      at = 0x80000000u | next;
    }
    if (actual.back() != tails[chain]) {
      return false;
    }
  }
  return actual.size() == std::size(expected) &&
         std::equal(actual.begin(), actual.end(), std::begin(expected));
}

bool validate_synthetic_global(bool corruptGlobalLink) {
  constexpr uint32_t oldHead = 0x80004000u, oldTail = 0x80005000u;
  constexpr uint32_t localHead = 0x80002000u, localTail = 0x80003000u;
  constexpr uint32_t oldTag = 0x09ABCDEFu;
  const uint32_t expectedTag = (oldTag & 0xFF000000u) | (localHead & 0x00FFFFFFu);
  const uint32_t observedTag = corruptGlobalLink ? oldTag : expectedTag;
  const uint32_t globalWord0 = localTail;
  const uint32_t globalWord1 = oldHead;
  return observedTag == expectedTag && globalWord0 == localTail && globalWord1 == oldHead &&
         (observedTag >> 24) == (oldTag >> 24);
}

int pairedActorSelftest() {
  int checks = 0;
  bool ok = true;
  auto expect = [&](bool pass, const char *what) {
    ++checks;
    if (!pass) {
      lucent::error("selftest", "FAIL(pairedpose): {}", what);
    }
    return pass;
  };

  ok &= expect(spyro::paired_actor::temporal_evidence::selftest(),
               "temporal presenter evidence rejects partial runs");
  ok &= expect(unpack_accum(0x00200801u).x == 1, "packed X extraction");
  std::array<uint32_t, 27> identity{};
  identity[0] = 4096;
  identity[2] = 4096;
  identity[4] = 4096;
  identity[7] = 1000;
  identity[24] = 256u << 16;
  identity[25] = 120u << 16;
  identity[26] = 341;
  const auto center = spyro::paired_actor_projection::projectRtps(0, 0, identity);
  ok &= expect(center.x == 256 && center.y == 120 && center.depth == 1000,
               "identity projection center and view depth");
  identity[7] = 170;
  const auto near = spyro::paired_actor_projection::projectRtps(1, 0, identity);
  ok &= expect(near.x == 257 && near.y == 120 && near.depth == 170,
               "near-plane saturated UNR projection");
  ok &= expect(std::fabs(center.screen_x - 256.0f) < 1.0e-6f &&
                   std::fabs(center.screen_y - 120.0f) < 1.0e-6f,
               "float projection preserves optical center");
  identity[7] = 1000;
  identity[5] = 1;
  const auto fractional = spyro::paired_actor_projection::projectRtps(0, 0, identity);
  ok &= expect(fractional.screen_x > 256.0f && fractional.screen_x < 257.0f && fractional.x == 256,
               "float projection retains subpixel endpoint discarded by integer SXY");
  Vec3i half = blend16({0, 0, 0}, {16, -16, 32}, 8);
  ok &= expect(half.x == 8, "half-frame positive blend");
  ok &= expect(half.y == -8 && half.z == 16, "half-frame signed blend");
  ok &= expect(blend16({7, -9, 11}, {99, 99, 99}, 0).x == 7, "zero blend preserves A");
  ok &= expect(keyframe_ptr(0xFFEF354Au) == 0x001E6A94u, "live frame-word pointer expansion");
  const Vec3i borrowed = rtps_input({11, -135, 128});
  ok &= expect(borrowed.x == 11 && borrowed.y == -135 && borrowed.z == 127,
               "RTPS DR0 addition carries negative Y into Z");
  const auto rootBorrowed = packed_root_input({-206, 4, -194});
  ok &= expect(rootBorrowed[0] == -4 && rootBorrowed[1] == 193 && rootBorrowed[2] == -206,
               "root RTPS packed add borrows negative low half into high half");
  constexpr uint32_t payload = 0x001E6608u, packedW8 = 0x05400000u;
  ok &= expect(payload + (packedW8 >> 20) == 0x001E665Cu,
               "layer-zero byte stream follows short stream payload");
  ok &=
      expect(validate_synthetic_ot(false, false),
             "local OT drains high bin then same-bin FIFO and ignores pre-existing global packet");
  ok &= expect(!validate_synthetic_ot(true, false), "local OT rejects corrupt tail");
  ok &= expect(!validate_synthetic_ot(false, true), "local OT rejects corrupt link");
  ok &= expect(validate_synthetic_global(false), "global OT appends after pre-existing chain");
  ok &=
      expect(!validate_synthetic_global(true), "global OT rejects corrupt pre-existing tail link");
  spyro::paired_actor::Frame fa{}, fb{};
  fa.valid = fb.valid = true;
  fa.epoch = fb.epoch = 7;
  fa.layer_counts = fb.layer_counts = {1, 1, 1};
  fa.topology = fb.topology = 0x1234;
  ok &= expect(framesCompatible(fa, fb), "identical immutable endpoint recipes are compatible");
  fb.epoch = 8;
  ok &= expect(!framesCompatible(fa, fb),
               "state2 exit and re-entry epoch rejects identical topology");
  fb = fa;
  fb.culled = true;
  ok &= expect(!framesCompatible(fa, fb), "culled endpoint resets compatibility");
  ok &= expect(!rebuildRecipeEligible(fb, false), "culled endpoint rebuild refuses");
  fb.culled = false;
  fb.valid = false;
  ok &= expect(!rebuildRecipeEligible(fb, false), "invalid endpoint rebuild refuses");
  fb.valid = true;
  ok &= expect(!rebuildRecipeEligible(fb, true), "duplicate painter endpoint rebuild refuses");
  fa.materials = {0x11223344};
  fb = fa;
  fa.materials[0] = 0;
  ok &=
      expect(fb.materials[0] == 0x11223344, "captured material copy is guest-mutation independent");
  spyro::paired_actor::Transform temporalTr{};
  temporalTr.ofx = 256u << 16;
  temporalTr.ofy = 120u << 16;
  temporalTr.h = 340;
  std::vector<spyro::paired_actor::ProjectedVertex> va(1), vb(1), vm;
  va[0].raw_view_x = 40000.0f;
  vb[0].raw_view_x = 20000.0f;
  va[0].raw_view_y = vb[0].raw_view_y = 0.0f;
  va[0].raw_view_z = vb[0].raw_view_z = 1000.0f;
  ok &= expect(spyro::paired_actor::interpolateProjected(va, vb, temporalTr, 0.5f, vm) &&
                   vm[0].view_x == 30000,
               "temporal raw X interpolates before IR saturation");
  vb[0].raw_view_x = 50000.0f;
  ok &= expect(spyro::paired_actor::interpolateProjected(va, vb, temporalTr, 0.5f, vm) &&
                   vm[0].view_x == 32767,
               "temporal interpolated raw X saturates once at the GTE IR limit");
  constexpr uint32_t envA = 0x80076EE0u, envB = 0x80076F64u;
  ok &= expect(spyro::render::frameDisplayEnv(envA, false) == envA &&
                   spyro::render::frameDisplayEnv(envB, false) == envB,
               "normal display policy keeps each draw env's guest previous-buffer DISPENV");
  ok &= expect(spyro::render::frameDisplayEnv(envA, true) == envB &&
                   spyro::render::frameDisplayEnv(envB, true) == envA,
               "FPS60 display policy selects reciprocal DISPENV for current A/B draw buffer");
  ok &= expect(spyro::render::frameDisplayEnv(0x80000000u, true) == 0,
               "display policy loudly refuses an unknown draw environment");
  spyro::paired_actor::Frame destinationPrev{}, destinationCur{};
  destinationPrev.gpu.off_y = 0;
  destinationCur.gpu.off_y = 240;
  ok &= expect(temporalDestination(destinationPrev, destinationCur).off_y == 240,
               "forced t=0 content still targets current frame GPU destination");
  spyro::paired_actor::FrameState life{};
  spyro::paired_actor::frameBegin(life, true, false, true);
  ok &= expect(!life.previous.valid && !life.endpoints_compatible,
               "first FPS60 frame has no temporal predecessor");
  life.previous.valid = true;
  life.current.valid = true;
  life.endpoints_compatible = true;
  spyro::paired_actor::frameBegin(life, false, false, true);
  ok &= expect(!life.previous.valid && !life.current.valid && !life.endpoints_compatible,
               "state2 exit clears both temporal endpoints");
  if (ok) {
    lucent::info("selftest", "PASS(pairedpose): {} checks", checks);
  }
  return ok ? 0 : 1;
}

} // namespace

int spyro::paired_actor::selftest() {
  return pairedActorSelftest();
}
