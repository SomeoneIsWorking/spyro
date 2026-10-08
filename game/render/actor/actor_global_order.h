#pragma once

#include "actor_draw_recipe.h"
#include "actor_prefix_builder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace spyro::actor_global_order {

enum class Status : uint8_t { Ready, ValidEmpty, InvalidRecord, InvalidLocalBin, InvalidMapping };

struct FaceKey {
  size_t faceIndex = 0;
  uint16_t otBin = 0;
  uint32_t recordOrdinal = 0;
  uint32_t chainOrdinal = 0;
};

struct Result {
  Status status = Status::ValidEmpty;
  const char *refusal = "none";
  std::vector<FaceKey> faces;
};

// Reconstructs the global OT bucket and within-record replay ordinal assigned
// by 0x8002074C..0x80020860 when one actor record's 288-entry local OT is
// coalesced into the frame OT. The source faces remain immutable and the
// result is indexed explicitly, so the caller can merge actors with the other
// authored frame producers without reading guest packets or either OT.
Result build(std::span<const actor_prefix::Output> records,
             std::span<const actor_draw_recipe::Face> faces);

} // namespace spyro::actor_global_order
