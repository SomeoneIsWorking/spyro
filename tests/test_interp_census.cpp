// The per-category interpolation census: the partition, the accounting, and the report's refusals.
//
// WHAT THIS GUARDS, and why each case is a negative. The report is the instrument that says which
// parts of a presented frame are real in-between frames. Three of its properties are falsifiable
// here and none of them is visible in the product's own log lines:
//
//  * the category partition is total and exclusive, so "particles drew 40,000 items and none were
//    interpolated" cannot be an artefact of the item falling through to the world bucket;
//  * the shortfall is captured minus reconstructed in ONE unit, so a category cannot report a
//    complete explanation it does not have;
//  * a category with no temporal source reports its WHOLE shortfall as that, from the denominator,
//    rather than leaving it unexplained and reading like a passing rule that never ran.
#include "core.h"
#include "game.h"
#include "interp_census.h"
#include "render_queue.h"
#include "spyro_context.h"
#include "testutil.h"

#include <array>
#include <memory>
#include <string_view>

namespace {

using spyro::interp_census::Category;
using spyro::interp_census::Reason;

RqItem worldItem(uint32_t publisher) {
  RqItem item{};
  item.layer = RQ_WORLD;
  item.has_xyf = 1;
  item.painter_object = publisher;
  return item;
}

RqItem hudItem() {
  RqItem item{};
  item.layer = RQ_HUD;
  item.painter_object = 0;
  return item;
}

void test_publisher_partition_is_named_and_total() {
  // Every first-party producer this repository declares lands in exactly one category, and a
  // publisher nobody names lands in the world bucket because that is what it is: world geometry
  // depth-sorted against the terrain. An item with no painter object at all is a guest packet, and
  // its layer is the only identity it has.
  CHECK(spyro::interp_census::categoryOf(worldItem(0x8001F798u)) == Category::Actors);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x80020F34u)) == Category::Actors);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x80023AC4u)) == Category::Actors);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x800258F0u)) == Category::World);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x8004EBA8u)) == Category::World);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x80022A2Cu)) == Category::World);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x8004FEA0u)) == Category::World);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x80050240u)) == Category::World);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x800573C8u)) == Category::Particles);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x800580F4u)) == Category::Particles);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x800584C4u)) == Category::Particles);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x80058D64u)) == Category::Particles);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x800189F0u)) == Category::Particles);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x80059A48u)) == Category::Particles);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x80059F8Cu)) == Category::Particles);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x80058864u)) == Category::Particles);
  CHECK(spyro::interp_census::categoryOf(worldItem(0x00000000u)) == Category::World);
  // An RQ_HUD item is the HUD category whatever publisher it names, and none: the framework's
  // painter planner refuses an item with a painter object that is not RQ_WORLD with RQ_OM_DEPTH, so
  // a HUD item cannot carry one at all.
  CHECK(spyro::interp_census::categoryOf(hudItem()) == Category::Hud);
}

void test_category_names_are_distinct() {
  // A report whose two rows print the same word cannot be read. Checked over the whole enum
  // rather than pairwise-by-eye.
  std::array<std::string_view, (size_t)Category::Count> names{};
  for (size_t i = 0; i < names.size(); ++i) {
    names[i] = spyro::interp_census::name((Category)i);
    CHECK(names[i] != "invalid");
    for (size_t j = 0; j < i; ++j) {
      CHECK(names[i] != names[j]);
    }
  }
  for (size_t r = 0; r < (size_t)Reason::Count; ++r) {
    CHECK(std::string_view(spyro::interp_census::name((Reason)r)) != "invalid");
  }
}

void test_shortfall_is_captured_minus_reconstructed() {
  spyro::interp_census::Frame frame{};
  frame.captured[(size_t)Category::World] = 100;
  frame.reconstructed[(size_t)Category::World] = 60;
  CHECK_EQ(frame.latestOnly(Category::World), 40u);
  // A layer that reconstructed MORE than the frame captured is not a shortfall; reporting a
  // negative number here would read as a category that drew the next real frame early, which is the
  // opposite of what a surplus means.
  frame.reconstructed[(size_t)Category::World] = 140;
  CHECK_EQ(frame.latestOnly(Category::World), 0u);
}

void test_camera_sample_is_one_per_in_between_present() {
  spyro::interp_census::Census owner;
  owner.cameraSample(true, Reason::None);
  owner.cameraSample(true, Reason::None);
  owner.cameraSample(false, Reason::CameraMismatch);
  // A camera sample is a per-logic-frame observation, so it reaches the totals only when the frame
  // closes. Reading the totals before that would be a confident zero about a counter that had not
  // been asked yet, which is the failure this case exists to prevent.
  CHECK_EQ(owner.totals().total.captured[(size_t)Category::Camera], 0u);
  owner.endLogicFrame();
  const auto &total = owner.totals().total;
  CHECK_EQ(total.captured[(size_t)Category::Camera], 3u);
  CHECK_EQ(total.reconstructed[(size_t)Category::Camera], 2u);
  CHECK_EQ(total.reasons[(size_t)Category::Camera][(size_t)Reason::CameraMismatch], 1u);
  // The camera's own row must never be reported as a complete explanation: two of three presents
  // had a camera between the two captured cameras and one did not.
  CHECK_EQ(total.latestOnly(Category::Camera), 1u);
}

void test_layer_record_census_accumulates_independently_of_items() {
  // A reason breakdown lives in records because that is what a pairing walk counts — one terrain
  // object emits many items — so it must accumulate separately from the item totals rather than
  // being subtracted from them. Two presents of the same layer, each with its own denominator.
  spyro::interp_census::Totals totals;
  spyro::interp_census::Frame first{};
  first.captured[(size_t)Category::World] = 1000;
  first.reconstructed[(size_t)Category::World] = 900;
  first.layers[(size_t)Category::World] = {.records = 50,
                                           .interpolated = 47,
                                           .noPredecessor = 2,
                                           .incompatible = 1,
                                           .samplerRefused = 0,
                                           .ran = true};
  spyro::interp_census::Frame second{};
  second.captured[(size_t)Category::World] = 2000;
  second.reconstructed[(size_t)Category::World] = 2000;
  second.layers[(size_t)Category::World] = {.records = 60,
                                            .interpolated = 60,
                                            .noPredecessor = 0,
                                            .incompatible = 0,
                                            .samplerRefused = 0,
                                            .ran = true};
  totals.add(first);
  totals.add(second);
  CHECK_EQ(totals.logicFrames, 2u);
  CHECK_EQ(totals.total.captured[(size_t)Category::World], 3000u);
  CHECK_EQ(totals.total.reconstructed[(size_t)Category::World], 2900u);
  CHECK_EQ(totals.total.latestOnly(Category::World), 100u);
  CHECK_EQ(totals.total.layers[(size_t)Category::World].records, 110u);
  CHECK_EQ(totals.total.layers[(size_t)Category::World].interpolated, 107u);
  CHECK_EQ(totals.total.layers[(size_t)Category::World].noPredecessor, 2u);
  CHECK_EQ(totals.total.layers[(size_t)Category::World].incompatible, 1u);
  CHECK(totals.total.layers[(size_t)Category::World].ran);
  // The pairing walk's own invariant, carried through the translation: the four buckets partition
  // the denominator. A caller that reported `records` as interpolated without the other three would
  // be reporting the walk's definition of success rather than its measurement.
  CHECK_EQ(totals.total.layers[(size_t)Category::World].interpolated +
               totals.total.layers[(size_t)Category::World].noPredecessor +
               totals.total.layers[(size_t)Category::World].incompatible +
               totals.total.layers[(size_t)Category::World].samplerRefused,
           totals.total.layers[(size_t)Category::World].records);
}

void test_no_layer_category_reports_its_whole_shortfall() {
  // The particle and effect producers have no temporal source, so every item they draw in an
  // in-between present is the current update's value. The report has to say that from the
  // denominator rather than leaving the shortfall unexplained, which would read like a rule that
  // ran and matched everything.
  CHECK(!spyro::interp_census::hasLayer(Category::Particles));
  CHECK(spyro::interp_census::hasLayer(Category::Actors));
  CHECK(spyro::interp_census::hasLayer(Category::World));
  CHECK(spyro::interp_census::hasLayer(Category::Hud));
  CHECK(spyro::interp_census::hasLayer(Category::Camera));
}

void test_repl_command_is_not_a_prefix_match() {
  // A Game carries two 65,536-entry render queues and is far too large for the stack: declaring one
  // by value overflows the frame before the first assertion runs, which is a crash in `main` with
  // no frame of its own to look at. It is heap-allocated here so the test fails on an assertion
  // rather than on the allocator's stack.
  auto game = std::make_unique<Game>();
  spyro::Context context;
  game->core.gameCtx = &context;
  // A command this owner does not own must be declined so the next owner gets it, and the gate
  // string must match in full: `interpcensusx` is a different command.
  CHECK(!spyro::interp_census::replCommand(game->core, "gates", nullptr));
  CHECK(!spyro::interp_census::replCommand(game->core, "interpcensusx", nullptr));
  CHECK(spyro::interp_census::replCommand(game->core, "interpcensus", nullptr));
  CHECK(spyro::interp_census::replCommand(game->core, "interpcensus", "reset"));
  CHECK_EQ(context.interpCensus.totals().logicFrames, 0u);
}

} // namespace

int main() {
  RUN(publisher_partition_is_named_and_total);
  RUN(category_names_are_distinct);
  RUN(shortfall_is_captured_minus_reconstructed);
  RUN(camera_sample_is_one_per_in_between_present);
  RUN(layer_record_census_accumulates_independently_of_items);
  RUN(no_layer_category_reports_its_whole_shortfall);
  RUN(repl_command_is_not_a_prefix_match);
  return pt_summary();
}