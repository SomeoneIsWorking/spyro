#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class Core;

namespace spyro::sparkle_recipe {

// The second half of 0x80058BA8: eight sparkle records at g_Sparkles, each drawn as two GP0 line
// primitives crossing at the sparkle's own projected position. Unlike every other producer this
// port owns, 0x800584C4 also ADVANCES the sparkles: it burns each one's lifetime and spins its
// angle by g_DeltaTime, and kills a sparkle it decides not to draw. The derivation stays pure and
// hands those writes back, so the state change happens at one named call rather than inside a
// projection loop.
constexpr std::size_t kRecords = 8;
constexpr std::uint32_t kRecordStride = 0x18u;
constexpr std::size_t kCorners = 4;

enum class Status : std::uint8_t {
  Ready,
  ValidEmpty,
  InvalidCore,
  InvalidState,
  InvalidProjection,
};

// Why a record produced no line. Counted rather than dropped silently: a frame where every sparkle
// is rejected has to be distinguishable from a frame with no sparkles alive.
enum class Reject : std::uint8_t {
  None,
  Expired,    // the lifetime ran out this tick
  NoLifetime, // the record's total lifetime is zero, so the fade has no denominator
  TooFar,     // the projected depth reached the record's own far limit
  TooNear,    // the projected depth is inside the 0x80 near limit
  Offscreen,  // the packed screen word left the retained window
  Count,
};

struct Vertex {
  std::int16_t sx = 0;
  std::int16_t sy = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
  float viewZ = 0.0f;
};

struct Line {
  std::array<Vertex, 2> vertices{};
  std::uint32_t colour = 0;
  std::uint16_t otBin = 0;
  std::uint32_t recordIndex = 0;
  // 0 for the line retail links first, 1 for the one behind it. Both belong to one sparkle.
  std::uint32_t chainOrdinal = 0;
};

// What 0x800584C4 writes back into a record as it draws. A killed sparkle keeps its angle: retail
// stores the new angle byte before it can decide to cull, and only then zeroes the lifetime.
struct StateWrite {
  std::uint32_t record = 0;
  std::uint8_t lifetime = 0;
  std::uint8_t angle = 0;
  bool angleWritten = false;
};

struct Recipe {
  Status status = Status::InvalidState;
  std::vector<Line> lines;
  std::vector<StateWrite> writes;
  std::uint32_t alive = 0; // records with lifetime left at entry
  std::uint32_t drawn = 0;
  std::array<std::uint32_t, (std::size_t)Reject::Count> rejects{};
};

// The packed SXY word retail tests whole rather than by component, so the two comparisons are kept
// as they are: a sparkle at Y=1, X<=0 is rejected by the first, not by a separate X rule.
bool onScreen(std::uint32_t packedScreen);
// The ordering-table bin: the projected SZ shifted right five, pulled six bins toward the viewer,
// floored at zero, and pushed 0x46 further back past 0x100.
std::int32_t otBin(std::uint16_t sz);

Recipe derive(Core *core, std::int32_t deltaTime);
// Applies the recipe's lifetime and angle writes. Separate from derive so the state change is one
// named step; retail performs it inline while drawing.
void commit(Core *core, const Recipe &recipe);
const char *statusName(Status status);

} // namespace spyro::sparkle_recipe
