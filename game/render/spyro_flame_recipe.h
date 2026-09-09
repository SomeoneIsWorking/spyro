#pragma once

#include <cstdint>
#include <vector>

class Core;

namespace spyro::flame_recipe {

enum class Status : std::uint8_t {
  Ready,
  ValidEmpty,
  InvalidCore,
  InvalidState,
  InvalidProjection,
};

// Why a part contributed nothing. Retail drops each of these silently, so counting them is the only
// way to tell an inactive flame from one the port failed to project.
enum class Reject : std::uint8_t {
  None,
  EmptyPart,
  TipCursorPastLimit,
  TipBehindCamera,
  TipBackfacing,
  RibbonNegativeBin,
  Count,
};

struct Vertex {
  std::int16_t sx = 0;
  std::int16_t sy = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
  float viewZ = 0.0f;
  std::uint16_t sz = 0;
};

// One emitted primitive. The tip fan is three untextured Gouraud vertices; the ribbon is a four
// point Gouraud textured quad in PSX corner order, so `nv` is the discriminator rather than two
// separate face vectors that would then need two parallel submitters.
struct Face {
  Vertex vertices[4]{};
  std::uint8_t nv = 3;
  std::uint8_t red[4]{};
  std::uint8_t green[4]{};
  std::uint8_t blue[4]{};
  std::uint8_t u[4]{};
  std::uint8_t v[4]{};
  bool textured = false;
  bool semi = false;
  std::uint16_t clut = 0;
  std::uint16_t tpage = 0;
  std::uint16_t otBin = 0;
  std::uint32_t part = 0;
  std::uint32_t faceOrdinal = 0;
};

struct Recipe {
  Status status = Status::ValidEmpty;
  std::vector<Face> faces;
  std::uint32_t parts = 0;   // parts with a positive length byte
  std::uint32_t tips = 0;    // tip fans that reached emission
  std::uint32_t ribbons = 0; // ribbon quads that reached emission
  std::uint32_t rejects[(std::size_t)Reject::Count]{};
};

// The ring radius 0x80058D64 scales its sine/cosine pair by, wide once the ribbon is past its first
// cross-section and narrow at the tip and the two closing steps.
std::int32_t ringScale(bool wide, bool superFlame);

// The grey the ribbon starts at, five steps darker per texture row already consumed.
std::uint8_t rampGrey(std::uint32_t rampIndex);

Recipe derive(Core *core);
const char *statusName(Status status);

} // namespace spyro::flame_recipe
