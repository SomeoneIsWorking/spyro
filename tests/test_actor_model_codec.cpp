#include "actor_model_codec.h"
#include "gte_state.h"

#include <array>
#include <cstdio>
#include <cstdlib>

extern "C" void GTE_Init(void);

namespace {
using namespace spyro::actor_model_codec;

unsigned checks = 0;

void require(bool condition, const char *what) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "actor_model_codec: %s (check %u)\n", what, checks);
    std::abort();
  }
}

uint32_t randomWord(uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

unsigned blendMismatches(const BlendResult &native, const GteRegs &guest) {
  unsigned mismatches = 0;
  for (unsigned i = 0; i < 3; ++i) {
    mismatches += native.mac[i] != (int32_t)guest.REG[25 + i];
    mismatches += native.ir[i] != (int16_t)guest.REG[9 + i];
  }
  return mismatches;
}

GteRegs blendGuest(Vec3i primary, Vec3i alternate, int16_t factor) {
  GteRegs guest{};
  guest.REG[8] = factor;
  guest.REG[9] = (uint32_t)primary.x;
  guest.REG[10] = (uint32_t)primary.y;
  guest.REG[11] = (uint32_t)primary.z;
  guest.REG[53] = (uint32_t)alternate.x;
  guest.REG[54] = (uint32_t)alternate.y;
  guest.REG[55] = (uint32_t)alternate.z;
  require(GTE_ExecuteIsolated(&guest, 0x4A980011u) >= 0, "isolated INTPL failed");
  return guest;
}

void checkBlend(Vec3i primary, Vec3i alternate, int16_t factor) {
  const BlendResult native = blendPose(primary, alternate, factor);
  const GteRegs guest = blendGuest(primary, alternate, factor);
  require(blendMismatches(native, guest) == 0, "INTPL output mismatch");
}

unsigned depthCueMismatches(const DepthCueResult &native, const GteRegs &guest) {
  unsigned mismatches = native.rgb != guest.REG[22];
  for (unsigned i = 0; i < 3; ++i) {
    mismatches += native.mac[i] != (int32_t)guest.REG[25 + i];
    mismatches += native.ir[i] != (int16_t)guest.REG[9 + i];
  }
  return mismatches;
}

GteRegs depthCueGuest(uint32_t rgb, std::array<int32_t, 3> farColor, int16_t factor) {
  GteRegs guest{};
  guest.REG[6] = rgb;
  guest.REG[8] = factor;
  for (unsigned i = 0; i < 3; ++i) {
    guest.REG[53 + i] = (uint32_t)farColor[i];
  }
  require(GTE_ExecuteIsolated(&guest, 0x4A780010u) >= 0, "isolated DPCS failed");
  return guest;
}

void checkDepthCue(uint32_t rgb, std::array<int32_t, 3> farColor, int16_t factor) {
  const DepthCueResult native = depthCueRgb(rgb, farColor, factor);
  const GteRegs guest = depthCueGuest(rgb, farColor, factor);
  require(depthCueMismatches(native, guest) == 0, "DPCS output mismatch");
}

void testStreamCodec() {
  const std::array<uint32_t, 2> full = {0x13579BDFu, 0x2468ACE0u};
  const std::array<int16_t, 2> delta = {(int16_t)0x8C63u, 2};
  const StreamResult decoded = decodeStream({0x2468ACE0u, full, delta, 5, 2});
  require(decoded.status == StreamStatus::Ok, "mixed full/delta stream rejected");
  require(decoded.vertices.size() == 5, "stream vertex denominator mismatch");
  require(decoded.vertices[0].x == 291 && decoded.vertices[0].y == -747 &&
              decoded.vertices[0].z == -1600,
          "first full word mismatch");
  require(decoded.vertices[1].x == 154 && decoded.vertices[1].y == 755 &&
              decoded.vertices[1].z == -2114,
          "second full word mismatch");
  require(decoded.vertices[2].x == 94 && decoded.vertices[2].y == 815 &&
              decoded.vertices[2].z == -2054,
          "signed delta mismatch");
  require(decoded.vertices[3].x == 94 && decoded.vertices[3].y == 815 &&
              decoded.vertices[3].z == -2058,
          "delta selector continuation mismatch");
  require(decoded.vertices[4].x == 291 && decoded.vertices[4].y == -747 &&
              decoded.vertices[4].z == -1600,
          "delta-to-full pointer progression mismatch");

  std::array<int16_t, 2> corruptDelta = delta;
  corruptDelta[0] = (int16_t)0x8463u;
  const StreamResult changed = decodeStream({0x2468ACE0u, full, corruptDelta, 5, 2});
  require(changed.status == StreamStatus::Ok && changed.vertices[2].x != decoded.vertices[2].x,
          "corrupt packed delta produced the same stream");

  const std::array<int16_t, 1> oddDelta = {2};
  const std::array<uint32_t, 1> oddFull = {0x2468ACE0u};
  const StreamResult oddFirst = decodeStream({0x13579BDFu, oddFull, oddDelta, 3, 0});
  require(oddFirst.status == StreamStatus::Ok && oddFirst.vertices[2].x == 291,
          "odd-first delta-to-full stream mismatch");
  const std::span<const int16_t> firstDelta(delta.data(), 1);
  const StreamResult shift0 = decodeStream({0x13579BDFu, {}, firstDelta, 2, 0});
  const StreamResult shift32 = decodeStream({0x13579BDFu, {}, firstDelta, 2, 32});
  require(shift0.status == StreamStatus::Ok && shift32.status == StreamStatus::Ok &&
              shift0.vertices[1].x == shift32.vertices[1].x &&
              shift0.vertices[1].y == shift32.vertices[1].y &&
              shift0.vertices[1].z == shift32.vertices[1].z,
          "MIPS shift mask 32!=0");
  const StreamResult shift31 = decodeStream({0x13579BDFu, {}, firstDelta, 2, 31});
  require(shift31.status == StreamStatus::Ok && shift31.vertices[1].x == -2147483494 &&
              shift31.vertices[1].y == -2147482893 && shift31.vertices[1].z == 2147481534,
          "shift31 wrap mismatch");
  require(decodeStream({0x2468ACE0u, {}, {}, 2, 0}).status == StreamStatus::FullUnderflow,
          "full underflow was accepted");
  require(decodeStream({0x13579BDFu, {}, {}, 2, 0}).status == StreamStatus::DeltaUnderflow,
          "delta underflow was accepted");
  require(decodeStream({0x2468ACE0u, full, {}, 1, 0}).status == StreamStatus::TrailingInput,
          "trailing full words were accepted");
  require(decodeStream({0x2468ACE0u, {}, {}, 0, 0}).status == StreamStatus::ZeroVertices,
          "zero-vertex stream was accepted");
}

void testVendorDifferential() {
  uint32_t seed = 0x91E10DA5u;
  for (unsigned i = 0; i < 1024; ++i) {
    const Vec3i primary{
        (int32_t)randomWord(seed), (int32_t)randomWord(seed), (int32_t)randomWord(seed)};
    const Vec3i alternate{
        (int32_t)randomWord(seed), (int32_t)randomWord(seed), (int32_t)randomWord(seed)};
    const int16_t factor = (int16_t)(randomWord(seed) % 16321u);
    checkBlend(primary, alternate, factor);
    const uint32_t rgb = randomWord(seed);
    const std::array<int32_t, 3> farColor = {
        (int32_t)randomWord(seed), (int32_t)randomWord(seed), (int32_t)randomWord(seed)};
    checkDepthCue(rgb, farColor, factor);
  }
  checkBlend({-32768, 32767, -1}, {32767, -32768, 0}, 16320);
  const Vec3i widePrimary{0x12340001, (int32_t)0x89ABFFFEu, 0x76548000};
  const Vec3i wideAlternate{0x10203040, -0x1020304, 0x55667788};
  checkBlend(widePrimary, wideAlternate, 0x2340);
  BlendResult corruptedBlend = blendPose(widePrimary, wideAlternate, 0x2340);
  const GteRegs blendOracle = blendGuest(widePrimary, wideAlternate, 0x2340);
  require(blendMismatches(corruptedBlend, blendOracle) == 0,
          "wide-primary corruption setup mismatch");
  ++corruptedBlend.mac[1];
  require(blendMismatches(corruptedBlend, blendOracle) == 1,
          "same INTPL comparator missed forced MAC corruption");
  checkDepthCue(0x7F00FF80u, {INT32_MIN, INT32_MAX, -1}, 16320);

  checkDepthCue(0xA5112233u, {0x100, 0x200, 0x300}, 2048);
  DepthCueResult cue = depthCueRgb(0xA5112233u, {0x100, 0x200, 0x300}, 2048);
  const GteRegs guest = depthCueGuest(0xA5112233u, {0x100, 0x200, 0x300}, 2048);
  require(depthCueMismatches(cue, guest) == 0, "forced-corruption setup mismatch");
  cue.rgb ^= 1u << 8;
  require(depthCueMismatches(cue, guest) == 1, "same DPCS comparator missed forced RGB corruption");
}

} // namespace

int main() {
  GTE_Init();
  testStreamCodec();
  testVendorDifferential();
  std::printf("actor_model_codec: PASS (%u checks)\n", checks);
  return 0;
}
