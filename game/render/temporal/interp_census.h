// What the interpolated 60fps presenter reconstructed, per draw category, with the reason for every
// draw it drew at its own endpoint instead.
//
// WHY THIS EXISTS. S020 read `partial` with the sentence "regular actors, shadows, particles and
// other unowned temporal sources lack complete matching-source interpolation", and every number
// above it was either a cumulative interpolated-PRIM count or a per-layer admission rate read out
// of that layer's own log line. Neither answers the question a player asks — "when I turn, which
// parts of the picture are real in-between frames and which are the next real frame delivered
// early" — and neither carries a denominator per category. One "3,607,931 interpolated prims" over
// "3,374 extra presents" is compatible with every layer being perfect and with one layer carrying
// all of it.
//
// TWO UNITS, BOTH LABELLED, AND NOT CONVERTED. The headline is queue ITEMS: the captured queue is
// what this logic frame drew and the reconstruction's own queue is what the in-between present
// rebuilt, so their difference is the shortfall in the same unit on both sides, and it covers all
// five categories including one that has no layer at all. A reason breakdown is only available at
// RECORD level, because that is what a layer's pairing walk counts — one terrain object emits many
// items — so it is reported under its own denominator rather than subtracted from an item count.
// Turning one into the other would be arithmetic dressed as measurement.
//
// THE CAMERA IS A ROW BECAUSE IT IS NOT A DRAW AND THE PICTURE NEEDS IT. The camera is not a
// producer: it is the view transform every world item is projected through, and this title bakes
// per-update camera motion into both the view rotation and each object's vertices. Its unit is one
// camera sample per in-between present, and its two answers are "the two endpoints' cameras were
// compatible, so this present's camera lies between them" and the reason when it was not.
//
// PRESENTATION ONLY. Nothing here writes guest state. The accounting is read at the two points the
// presenter already reaches: the logic frame's admission and its reconstruction.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class Core;
struct RqItem;

namespace spyro::interp_census {

// The five categories a presented frame is accounted into. They partition every captured item, and
// the partition is by PUBLISHER: the world, actor and shaded producers all draw into RQ_WORLD, so
// a layer test would merge three categories into one.
enum class Category : uint8_t {
  Camera = 0,
  Actors,
  World,
  Particles,
  Hud,
  Count,
};
inline constexpr size_t kCategoryCount = (size_t)Category::Count;

// Why a draw was presented at its own endpoint rather than sampled between two captured states.
// `None` is the compatible case. Every other value names the exact rule that decided it, because
// "not interpolated" cannot be told from "the rule never ran" — the failure this project has now
// produced nine times, in two directions.
enum class Reason : uint8_t {
  None = 0,
  NoTemporalSource,    // no layer reconstructs this publisher at all
  IntervalNotAdmitted, // the layer exists, and its preflight refused this interval
  NoPredecessor,       // attributed to an instance the previous update did not draw
  Incompatible,        // a predecessor existed and the layer's identity rule rejected it
  SamplerRefused,      // the layer's sampler declined the pair
  CameraMismatch,      // the two endpoints' camera / frame provenance disagree
  Count,
};
inline constexpr size_t kReasonCount = (size_t)Reason::Count;

const char *name(Category category);
const char *name(Reason reason);

// Whether any temporal layer reconstructs this category. A category with no layer is not a
// temporary failure; it is the coverage gap the report exists to name, and its whole shortfall is
// reported as `NoTemporalSource` from the shortfall itself.
bool hasLayer(Category category);

// Which category an item belongs to. The publisher is the item's painter object, which every
// first-party world producer sets through `ProducerScope`; the HUD category is the layer, because
// an RQ_HUD item cannot carry one — the framework's painter planner refuses any item with a painter
// object that is not RQ_WORLD with RQ_OM_DEPTH
// (external/psxport/runtime/psx/painter_object_layer.cpp:12-14) and the render queue treats a
// refused plan as fatal. That is why the 2D overlay's claim in `TemporalScene` is layer-keyed too.
//
// This is the category partition, not the ownership decision. `TemporalScene::owns` asks a
// different question — is this item replaced in the present being drawn, which depends on each
// layer's own admission — and one category's layers are not admitted together.
Category categoryOf(const RqItem &item);

// One layer's pairing walk over ONE in-between present, in records. `records` is its denominator
// and the other four buckets partition it, so `interpolated + noPredecessor + incompatible +
// refused == records` whenever the layer ran, and `records` is reported whole when it did not.
struct LayerCensus {
  uint32_t records = 0;
  uint32_t interpolated = 0;
  uint32_t noPredecessor = 0;
  uint32_t incompatible = 0;
  uint32_t samplerRefused = 0;
  bool ran = false;
};

// One logic frame's account, and the running total over the route.
struct Frame {
  std::array<uint32_t, kCategoryCount> captured{};
  std::array<uint32_t, kCategoryCount> reconstructed{};
  // Item-level reasons. Only the categories with no layer and the camera carry one here.
  std::array<std::array<uint32_t, kReasonCount>, kCategoryCount> reasons{};
  // Record-level reasons, accumulated per category and reported with their own denominator.
  LayerCensus layers[kCategoryCount];

  uint32_t latestOnly(Category category) const {
    const size_t c = (size_t)category;
    return captured[c] > reconstructed[c] ? captured[c] - reconstructed[c] : 0u;
  }
};

// The route's running total, plus the presented-frame counters: one real present per game update
// and, while the source is eligible, one in-between present. The product counts them at the
// presentation boundary; this counts what each one was made of.
struct Totals {
  uint64_t logicFrames = 0;
  uint64_t presentedReal = 0;
  uint64_t presentedInterpolated = 0;
  Frame total{};

  void add(const Frame &frame);
};

// The owner. It lives in the per-Core context, not in a file-scope object, so two Cores in one
// process cannot share one route's counters.
class Census {
public:
  // The logic frame's admission: one walk of the captured queue, which is the denominator.
  void beginLogicFrame(Core &core);
  // Whether a reconstruction pass at `t` is the in-between present. The pass at 1 is the real
  // present's reconstruction, whose geometry is by definition the current endpoint, so it is the
  // denominator's other half and never part of the interpolated numerator.
  static bool isInBetween(float t) {
    return t < 1.0f;
  }
  // The reconstruction's OWN queue, walked once every layer has published into it. `t` is the
  // presenter's interpolation factor; a pass at 1 contributes nothing here.
  void reconstruct(Core &core, float t);
  void endLogicFrame();

  // One layer's record census for the present being reconstructed.
  void recordLayer(Category category, const LayerCensus &census);

  // One in-between present's camera sample. `compatible` says the two endpoints' cameras agreed;
  // `reason` says why not when they did not.
  void cameraSample(bool compatible, Reason reason);

  const Totals &totals() const {
    return totals_;
  }

  // Every category's share, with its own denominator, so a zero is distinguishable from a category
  // that never drew.
  void report(const char *when) const;

  void reset() {
    *this = Census();
  }

private:
  Totals totals_{};
  Frame current_{};
};

// REPL: `interpcensus` prints the running total with denominators; `interpcensus reset` clears it.
bool replCommand(Core &core, const char *command, const char *line);

} // namespace spyro::interp_census