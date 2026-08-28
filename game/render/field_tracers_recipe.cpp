#include "field_tracers_recipe.h"

#include <cstddef>
#include <utility>

namespace spyro::field_tracers_recipe {
namespace {

constexpr uint32_t kTracerCount = 0x80075684u;
constexpr uint32_t kTracerPointCount = 0x800772c8u;
constexpr uint32_t kTracerLists = 0x80078658u;
constexpr uint32_t kTracerCapacity = 4u;
constexpr uint32_t kPointSize = 0x1cu;

Recipe refuse(Recipe out, const char *why) {
  out.status = Status::InvalidPointers;
  out.refusal = why;
  out.chains.clear();
  return out;
}

} // namespace

Recipe derive(const world_chunk_codec::RamView &ram) {
  Recipe out{};
  if (!ram.contains(kTracerCount, 4u) || !ram.contains(kTracerPointCount, kTracerCapacity * 4u) ||
      !ram.contains(kTracerLists, kTracerCapacity * 4u)) {
    return refuse(std::move(out), "tracer_tables");
  }
  const int32_t count = (int32_t)ram.r32(kTracerCount);
  if (count <= 0) {
    return out;
  }
  if ((uint32_t)count > kTracerCapacity) {
    return refuse(std::move(out), "tracer_count");
  }
  out.tracerCount = (uint32_t)count;
  out.chains.reserve((uint32_t)count);
  for (uint32_t i = 0; i < (uint32_t)count; ++i) {
    const int32_t pointCount = (int32_t)ram.r32(kTracerPointCount + i * 4u);
    const uint32_t list = ram.r32(kTracerLists + i * 4u);
    if (pointCount <= 0) {
      out.chains.push_back({});
      continue;
    }
    if ((uint32_t)pointCount > UINT32_MAX / kPointSize ||
        !ram.contains(list, (uint32_t)pointCount * kPointSize)) {
      return refuse(std::move(out), "tracer_points");
    }
    Chain chain;
    chain.points.reserve((uint32_t)pointCount);
    for (uint32_t j = 0; j < (uint32_t)pointCount; ++j) {
      const uint32_t address = list + j * kPointSize;
      chain.points.push_back(Point{address,
                                   (int32_t)ram.r32(address),
                                   (int32_t)ram.r32(address + 4u),
                                   (int32_t)ram.r32(address + 8u),
                                   (int32_t)ram.r32(address + 24u)});
    }
    out.chains.push_back(std::move(chain));
  }
  out.status = Status::Ready;
  return out;
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid_empty";
  case Status::InvalidPointers:
    return "invalid_pointers";
  }
  return "unknown";
}

} // namespace spyro::field_tracers_recipe
