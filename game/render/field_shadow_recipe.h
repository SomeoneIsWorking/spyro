#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class Core;

namespace spyro::field_shadow_recipe {

constexpr std::size_t kFanPoints = 16;

enum class Status : std::uint8_t {
  Ready,
  ValidEmpty,
  InvalidCore,
  InvalidState,
  InvalidProjection,
};

struct Vertex {
  std::int16_t sx = 0;
  std::int16_t sy = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
  float viewZ = 0.0f;
  std::uint16_t sz = 0;
};

struct Face {
  std::array<Vertex, 3> vertices{};
  std::uint16_t otBin = 0;
  std::uint32_t fanOrdinal = 0;
};

struct Recipe {
  Status status = Status::InvalidState;
  std::array<Face, kFanPoints> faces{};
  std::size_t faceCount = 0;
};

std::int32_t
otBin(std::uint16_t firstSz, std::uint16_t secondSz, std::uint16_t anchorSz, std::int32_t bias);
std::uint8_t interpolateRadius(std::uint8_t current, std::uint8_t next, std::uint8_t progress);
Recipe derive(Core *core);
const char *statusName(Status status);

} // namespace spyro::field_shadow_recipe
