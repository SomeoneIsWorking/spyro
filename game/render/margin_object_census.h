#pragma once

// Per-class census of the objects a field DRAWS past the guest's own 512-column window.
//
// A widened frame can be wrong in two ways a single "the margin is non-black" number cannot tell
// apart: the widening may not reach a producer at all (margin black), or it may reach it and the
// margin still be black for an unrelated reason. This instrument answers the producer-level
// question instead — per class, how many drawn objects had a span leaving the guest window, and
// how far left and right they reached — so a 16:9 run and the matched 4:3 run are two answers to
// the same question rather than one number and a guess.
//
// It counts OBJECTS, not pixels: it is fed at each producer's submission point with the drawn span
// that producer actually rasterised, so "revealed" means an object was drawn, not that a pixel
// changed colour. That distinction is the point — a pixel that was already non-black and stays
// non-black cannot move a pixel census, so pixels are blind to exactly the case this exists for.

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>

namespace spyro::margin_object_census {

// One owner per producer that can put an object on screen. The set is closed and named; a new
// producer is added here deliberately, which is what makes a zero in the report meaningful rather
// than an unrecorded class silently absent.
enum class Class : uint32_t {
  kSector = 0,
  kMoby = 1,
  kParticle = 2,
  kGlow = 3,
  kShadow = 4,
  kCount = 5,
};

inline constexpr size_t kClassCount = static_cast<size_t>(Class::kCount);

const char *className(Class c);

// One class's running answer. A plain value type with no Core, no GPU and no ambient dependency, so
// a recipe that by design has none can still carry one and be drained by whoever does have a Core.
struct SpanBucket {
  // Objects of this class the port drew, and how many of those had a span leaving [0, 512).
  uint32_t drawn = 0;
  uint32_t outsideGuest = 0;
  // The leftmost and rightmost screen x this class actually drew at. These are the falsifier: a
  // 16:9 run whose rightmost x is still under 512 for a class is a class the widening never
  // reached, however healthy its pixel coverage looks.
  int32_t minX = INT32_MAX;
  int32_t maxX = INT32_MIN;

  void add(int32_t x0, int32_t x1);
  // One object whose drawn span is the extent of the screen x of [first, last), read through
  // `screenX`. Every producer that rasterises vertices feeds the census through this, so the
  // extent is taken once. An empty range records nothing. Linear in the vertex count, no
  // allocation.
  template <typename It, typename Projection = std::identity>
  void addVertices(It first, It last, Projection screenX = {}) {
    const auto [lo, hi] = std::ranges::minmax_element(first, last, {}, screenX);
    if (lo != last) {
      add(static_cast<int32_t>(std::invoke(screenX, *lo)),
          static_cast<int32_t>(std::invoke(screenX, *hi)));
    }
  }
  void merge(const SpanBucket &other);
  bool empty() const {
    return drawn == 0u;
  }
};

// Accumulates across a run. Held by spyro::Context: no globals, no statics, and its lifetime is
// exactly the context's.
class Recorder {
public:
  void record(Class c, int32_t x0, int32_t x1) {
    tallies_[static_cast<size_t>(c)].add(x0, x1);
  }
  template <typename It, typename Projection = std::identity>
  void addVertices(Class c, It first, It last, Projection screenX = {}) {
    tallies_[static_cast<size_t>(c)].addVertices(first, last, screenX);
  }
  void merge(Class c, const SpanBucket &b) {
    tallies_[static_cast<size_t>(c)].merge(b);
  }

  const SpanBucket &tally(Class c) const {
    return tallies_[static_cast<size_t>(c)];
  }
  bool empty() const;

  // Both answers on one line per class, with denominators: the count drawn, the count outside the
  // guest window, and the reach. A class that drew nothing says "0 drawn" rather than vanishing, so
  // an absent class is distinguishable from one that was never wired up.
  std::string report() const;

private:
  std::array<SpanBucket, kClassCount> tallies_{};
};

// Writes `report()` to the path named by PSXPORT_MARGIN_CENSUS, read through the framework's cfg
// owner; a run without it writes no file and pays one branch per object.
void writeReportIfRequested(const Recorder &recorder);

} // namespace spyro::margin_object_census
