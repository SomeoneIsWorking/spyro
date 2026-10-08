#pragma once

#include "actor_prefix_builder.h"
#include "actor_recipe_capture.h"

#include <cstdint>

namespace spyro::test_fixture {

// One packed vertex of the 0x8001F798 model stream. The format's fields overlap at bit 21, where
// the first component's low bit is the second's sign, so this helper keeps the second and third
// components inside their positive range and leaves the selector bit clear, which is what makes
// the following word a full vertex rather than a delta.
constexpr uint32_t packVertex(int32_t first, int32_t second, int32_t third) {
  return (((uint32_t)first & 0x7ffu) << 21) | (((uint32_t)second & 0x3ffu) << 11) |
         (((uint32_t)third & 0x3ffu) << 1);
}

// One authored actor: a single front-facing triangle that composes to exactly one drawn face, at
// the position `tx` and the depth `tz`. Everything that identifies the model is identical across
// calls, so two of these differing only in position are the same actor having moved, and the only
// thing a temporal sample can change is where it is.
inline actor_recipe_capture::Record actorRecord(uint32_t moby, int32_t tx, int32_t tz = 4096) {
  actor_recipe_capture::Record record{};
  record.moby = moby;
  record.descriptor = 0x80070000u;
  auto &input = record.input;
  input.tx = tx;
  input.ty = 0;
  input.tz = tz;
  input.matrixWords = {0x00001000u, 0, 0x00001000u, 0, 0x00001000u};
  input.vertexCount = 3;
  input.primary.firstFull = packVertex(0, 0, 0);
  input.primary.fullWords = {packVertex(0, 600, 0), packVertex(0, 0, 600)};
  input.colorArm = actor_prefix::ColorArm::High;
  input.primaryColors = {0x00112233u};
  input.primitiveWords = {0x00002020u, 0};
  input.projection = {.ofx = 160 << 16, .ofy = 120 << 16, .h = 256};
  record.expected = actor_prefix::build(input);
  record.expected.moby = moby;
  return record;
}

} // namespace spyro::test_fixture
