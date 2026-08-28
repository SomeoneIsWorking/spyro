#pragma once

#include "world_chunk_codec.h"

#include <cstdint>
#include <vector>

namespace spyro::field_tracers_recipe {

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidPointers,
};

struct Point {
  uint32_t address = 0;
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
  int32_t age = 0;
};

struct Chain {
  std::vector<Point> points;
};

struct Recipe {
  Status status = Status::ValidEmpty;
  const char *refusal = "none";
  uint32_t tracerCount = 0;
  std::vector<Chain> chains;
};

// Decode the guest's tracer pointer/count tables. The native renderer owns only the projection and
// primitive construction; this recipe keeps the source lists and their 28-byte point records
// explicit.
Recipe derive(const world_chunk_codec::RamView &ram);
const char *statusName(Status status);

} // namespace spyro::field_tracers_recipe
