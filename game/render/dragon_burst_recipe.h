#pragma once

#include <array>
#include <cstdint>
#include <vector>

class Core;

// 0x80058864, the burst 0x8001CFDC draws before every one of its states while D_80076248 is armed.
// It is an eight-spoke star built entirely from two sine-table rings around one projected origin:
// an inner ring of eight points, an outer ring of eight offset half a step and four times the
// radius, and a fan back to the centre. It links into g_HudOT rather than the world table, so it is
// a 2D overlay drawn after the scene rather than a depth-sorted world producer.
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
