#pragma once

#include <array>
#include <cstdint>
#include <vector>

class Core;

// The burst 0x80058864: an eight-spoke star of two sine-table rings around one projected origin,
// linked into g_HudOT so it is a 2D overlay rather than a depth-sorted world producer.
namespace spyro::dragon_burst {

constexpr std::size_t kSpokes = 8;

enum class Status : std::uint8_t {
  Ready,
  Inactive,
  InvalidCore,
  InvalidProjection,
};

struct Vertex {
  std::int16_t sx = 0;
  std::int16_t sy = 0;
};

struct Triangle {
  std::array<Vertex, 3> vertices{};
};

struct Recipe {
  Status status = Status::InvalidCore;
  std::vector<Triangle> triangles;
  std::uint8_t colour = 0;
};

Recipe derive(Core *core);
const char *statusName(Status status);

} // namespace spyro::dragon_burst
