#include "core.h"
#include "fps60.h"
#include "game.h"
#include "paired_actor_depth.h"
#include "scene_painter_order.h"
#include "spyro_context.h"
#include "temporal_scene.h"
#include "temporal_scene_source.h"
#include "testutil.h"
#include "title_runtime_registry.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace {

void test_exact_producer_membership() {
  const auto source = spyro_temporal_scene_source();
  RqItem item{};
  item.layer = RQ_WORLD;
  item.has_xyf = true;
  item.painter_object = 0x80023AC4u;
  CHECK(source->owns(item));
  item.painter_object = 0x800258F0u;
  CHECK(!source->owns(item));
  item.painter_object = 0x80023AC4u;
  item.has_xyf = false;
  CHECK(!source->owns(item));
  item.has_xyf = true;
  item.layer = RQ_BACKGROUND;
  CHECK(!source->owns(item));
}

void test_runtime_factory_rotates_own_endpoints() {
  auto game = std::make_unique<Game>();
  SpyroContext context;
  game->core.gameCtx = &context;
  auto &state = context.pairedActor;
  state.current.valid = true;
  state.current.epoch = 7u;
  state.current.pose.push_back({1, 2, 3});
  state.temporal_eligible = true;
  state.endpoints_compatible = true;

  auto &runtime = spyro::runtimeFor(spyro::SpyroTitle::Spyro1);
  auto presentation = runtime.createTemporalFramePresentation(*game);
  auto *temporal = dynamic_cast<Fps60 *>(presentation.get());
  CHECK(temporal != nullptr);
  CHECK(game->core.hooks == nullptr);
  temporal->presentRotate();
  CHECK(state.previous.valid);
  CHECK_EQ(state.previous.epoch, 7u);
  CHECK_EQ(state.previous.pose.size(), 1u);
  CHECK(!state.current.valid);
  CHECK(!state.endpoints_compatible);
  CHECK(!state.temporal_eligible);

  // Missing/refused current data must invalidate history, never replay the prior actor forever.
  temporal->presentRotate();
  CHECK(!state.previous.valid);
}

void test_disabled_and_discontinuous_frames_refuse() {
  auto game = std::make_unique<Game>();
  SpyroContext context;
  game->core.gameCtx = &context;
  auto source = spyro_temporal_scene_source();
  auto &state = context.pairedActor;
  state.temporal_eligible = true;
  spyro_temporal_scene_prepare(game->core);
  CHECK(!source->eligible(game->core));
  state.was_fps60_active = true;
  state.temporal_eligible = true;
  spyro_temporal_scene_prepare(game->core);
  CHECK(!source->eligible(game->core));
  state.endpoints_compatible = true;
  state.temporal_eligible = true;
  // Compatibility flags cannot authorize missing immutable endpoints.
  spyro_temporal_scene_prepare(game->core);
  CHECK(!source->eligible(game->core));
  CHECK_EQ(state.temporal.eligibility_checks, 1u);
}

void setSourceDepth(SpyroPairedFrame &frame, int32_t z, uint32_t control = 0, uint8_t bias = 0) {
  const auto depth = spyro::paired_actor_depth::derive(z, bias, control);
  frame.transform.base_mac[2] = z;
  frame.transform.layer_cr[0][7] = static_cast<uint32_t>(z);
  frame.transform.ot_control = control;
  frame.transform.depth_bias = bias;
  frame.transform.depth_origin = static_cast<uint32_t>(depth.origin);
  frame.transform.depth_near = depth.near;
  frame.transform.ot_shift = depth.shift;
}

SpyroPairedFrame triangle() {
  SpyroPairedFrame frame{};
  frame.valid = true;
  frame.authored_replay = true;
  frame.epoch = 1u;
  frame.topology = 1u;
  frame.layer_counts = {3u, 0u, 0u};
  frame.pose = {{0, -100, -100}, {0, 100, -100}, {0, 0, 100}};
  frame.transform.layer_cr[0] = {4096u, 0u, 4096u, 0u, 4096u, 0u, 0u, 1000u};
  frame.transform.ofx = 256u << 16u;
  frame.transform.ofy = 120u << 16u;
  frame.transform.h = 341u;
  setSourceDepth(frame, 1000);
  frame.materials = {0x00FFFFFFu};
  spyro::paired_actor::Primitive primitive{};
  primitive.two_sided = true;
  primitive.projected_offset[1] = 4u;
  primitive.projected_offset[2] = 8u;
  frame.primitives.push_back(primitive);
  frame.gpu.da_x1 = 511;
  frame.gpu.da_y1 = 239;
  return frame;
}

void checkSourceUnchanged(const SpyroPairedFrame &before, const SpyroPairedFrame &after) {
  CHECK_EQ(before.valid, after.valid);
  CHECK_EQ(before.culled, after.culled);
  CHECK_EQ(before.epoch, after.epoch);
  CHECK_EQ(before.topology, after.topology);
  CHECK_EQ(before.authored_replay, after.authored_replay);
  CHECK(before.layer_counts == after.layer_counts);
  CHECK(before.pose == after.pose);
  CHECK(before.materials == after.materials);
  CHECK_EQ(before.override_control, after.override_control);
  CHECK(before.transform.layer_cr == after.transform.layer_cr);
  CHECK(before.transform.base_mac == after.transform.base_mac);
  CHECK_EQ(before.transform.depth_origin, after.transform.depth_origin);
  CHECK_EQ(before.transform.depth_near, after.transform.depth_near);
  CHECK_EQ(before.transform.depth_bias, after.transform.depth_bias);
  CHECK_EQ(before.transform.ot_control, after.transform.ot_control);
  CHECK_EQ(before.transform.ot_shift, after.transform.ot_shift);
  CHECK_EQ(before.primitives.size(), after.primitives.size());
  for (size_t i = 0; i < std::min(before.primitives.size(), after.primitives.size()); ++i) {
    const auto &a = before.primitives[i];
    const auto &b = after.primitives[i];
    CHECK_EQ(a.source_ordinal, b.source_ordinal);
    CHECK_EQ(a.quad, b.quad);
    CHECK_EQ(a.two_sided, b.two_sided);
    CHECK_EQ(a.semi_transparent, b.semi_transparent);
    CHECK_EQ(a.ot_adjust, b.ot_adjust);
    CHECK(std::equal(std::begin(a.projected_offset),
                     std::end(a.projected_offset),
                     std::begin(b.projected_offset)));
    CHECK(std::equal(
        std::begin(a.material_offset), std::end(a.material_offset), std::begin(b.material_offset)));
    CHECK(
        std::equal(std::begin(a.packet_attr), std::end(a.packet_attr), std::begin(b.packet_attr)));
  }
}

void test_mixed_scene_reconstructs_authored_motion_without_guest_writes() {
  auto game = std::make_unique<Game>();
  SpyroContext context;
  game->core.gameCtx = &context;
  game->mods.fps60 = true;
  auto &state = context.pairedActor;
  state.previous = triangle();
  state.current = state.previous;
  state.current.transform.layer_cr[0][5] = 100u;
  state.endpoints_compatible = true;
  state.was_fps60_active = true;
  CHECK(spyro_paired_actor_rebuild_endpoint(&game->core, game->rq, state.current) ==
        SpyroPairedRebuildResult::Emitted);
  CHECK_EQ(game->rq.n, 1);
  if (game->rq.n != 1) {
    return;
  }
  const RqItem endpoint = game->rq.items[0];
  const uint16_t bin = endpoint.painter_replay.key.ot_bin;
  CHECK(bin > 0 && bin < UINT16_MAX);
  // One FIELD flush: multiple world faces and Spyro share the authored domain. Submission
  // order deliberately disagrees with OT order, including the world/Spyro equal-bin tie.
  std::array<RqItem, 4> captured{endpoint, endpoint, endpoint, endpoint};
  for (size_t i = 0; i < captured.size(); ++i) {
    captured[i].seq = captured[i].draw_seq = static_cast<uint32_t>(10 + i);
    captured[i].painter_object = 0x800258F0u;
  }
  captured[0].painter_replay = spyro::scene_painter_order::world(bin + 1, 0, 0);
  captured[1].painter_object = endpoint.painter_object;
  captured[2].painter_replay = spyro::scene_painter_order::world(bin, 1, 0);
  captured[3].painter_replay = spyro::scene_painter_order::world(bin - 1, 2, 0);
  const std::array<const RqItem *, 4> original{
      &captured[0], &captured[1], &captured[2], &captured[3]};
  const auto baseline = planPainterItemStream(original);
  CHECK(baseline.accepted());
  CHECK_EQ(baseline.commands.size(), 4u);

  spyro_temporal_scene_prepare(game->core);
  CHECK(state.temporal_eligible);
  const SpyroPairedFrame previousBefore = state.previous, currentBefore = state.current;
  const std::vector<uint8_t> ramBefore(std::begin(game->core.ram), std::end(game->core.ram));
  Fps60 presentation(*game, spyro_temporal_scene_source());
  float midpoint = 0;
  for (float t : {0.5f, 1.0f}) {
    const int range = game->gpu_vk.s_painter_ranges;
    presentation.presentPass(&game->core, t, {captured, 1});
    CHECK_EQ(presentation.mSink->n, 1);
    CHECK_EQ(presentation.mPresentStream.size(), 4u);
    const auto plan = planPainterItemStream(presentation.mPresentStream);
    CHECK(plan.accepted());
    CHECK_EQ(plan.ranges.size(), 1u);
    CHECK_EQ(plan.commands.size(), baseline.commands.size());
    if (plan.commands.size() != 4 || baseline.commands.size() != 4) {
      continue;
    }
    const std::array<size_t, 4> expected{0, 2, 1, 3};
    for (size_t i = 0; i < expected.size(); ++i) {
      const auto *item = presentation.mPresentStream[plan.commands[i].item_index];
      CHECK_EQ(plan.commands[i].object, baseline.commands[i].object);
      CHECK_EQ(baseline.commands[i].item_index, expected[i]);
      if (expected[i] != 1) {
        CHECK(item == &captured[expected[i]]); // unrelated source survives byte-for-byte
      } else {
        CHECK(item == &presentation.mSink->items[0]);
        CHECK_EQ(item->painter_replay.domain, endpoint.painter_replay.domain);
        CHECK_EQ(item->painter_replay.key.ot_bin, bin);
        if (t == 0.5f) {
          midpoint = item->xsf[0];
        } else {
          CHECK(midpoint < item->xsf[0]);
          CHECK_EQ(item->xsf[0], endpoint.xsf[0]);
        }
      }
    }
    // These are the commands staged by presentPass itself. Adjacent world triangles coalesce,
    // but the paired object must stay between the tied/far world faces and the near world face.
    CHECK_EQ(game->gpu_vk.s_painter_ranges, range + 1);
    CHECK_EQ(game->gpu_vk.s_painter_count[range], 3);
    const int first = game->gpu_vk.s_painter_first[range];
    CHECK_EQ(game->gpu_vk.s_painter_cmd_object[first], 0x800258F0u);
    CHECK_EQ(game->gpu_vk.s_painter_cmd_count[first], 6);
    CHECK_EQ(game->gpu_vk.s_painter_cmd_object[first + 1], 0x80023AC4u);
    CHECK_EQ(game->gpu_vk.s_painter_cmd_object[first + 2], 0x800258F0u);
    CHECK(game->rqRedirect == nullptr);
    CHECK(!game->core.rsub.mode.displayPassArmed());
  }
  CHECK(spyro_paired_temporal_proven(state.temporal));
  CHECK(std::equal(ramBefore.begin(), ramBefore.end(), std::begin(game->core.ram)));
  checkSourceUnchanged(previousBefore, state.previous);
  checkSourceUnchanged(currentBefore, state.current);
  CHECK_EQ(game->rq.n, 1);
}

void test_valid_empty_endpoint_keeps_visible_midpoint() {
  auto game = std::make_unique<Game>();
  SpyroContext context;
  game->core.gameCtx = &context;
  game->mods.fps60 = true;
  auto &state = context.pairedActor;
  state.previous = triangle();
  state.previous.primitives[0].two_sided = false;
  state.current = state.previous;
  // The third vertex passes through the first edge: the source face is front-facing at the
  // previous endpoint and midpoint, then back-facing at t=1. Topology and provenance are unchanged.
  state.current.pose[2][2] = -150;
  state.endpoints_compatible = true;
  state.was_fps60_active = true;
  CHECK(spyro_paired_actor_rebuild_endpoint(&game->core, game->rq, state.previous) ==
        SpyroPairedRebuildResult::Emitted);
  CHECK_EQ(game->rq.n, 1);
  game->rq.reset();
  CHECK(spyro_paired_actor_rebuild_endpoint(&game->core, game->rq, state.current) ==
        SpyroPairedRebuildResult::NoOutput);
  CHECK_EQ(game->rq.n, 0);
  spyro_temporal_scene_prepare(game->core);
  CHECK(state.temporal_eligible);
  CHECK_EQ(state.temporal.eligible_intervals, 1u);
  const SpyroPairedFrame previousBefore = state.previous, currentBefore = state.current;
  const std::vector<uint8_t> ramBefore(std::begin(game->core.ram), std::end(game->core.ram));
  const std::vector<uint8_t> scratchBefore(std::begin(game->core.scratch),
                                           std::end(game->core.scratch));
  Fps60 presentation(*game, spyro_temporal_scene_source());
  presentation.presentPass(&game->core, 0.5f, {});
  CHECK_EQ(presentation.mPresentStream.size(), 1u);
  CHECK_EQ(presentation.mSink->n, 1);
  presentation.presentPass(&game->core, 1.0f, {});
  CHECK(presentation.mPresentStream.empty());
  CHECK_EQ(presentation.mSink->n, 0);
  CHECK_EQ(state.temporal.midpoint_calls, 1u);
  CHECK_EQ(state.temporal.endpoint_calls, 1u);
  CHECK_EQ(state.temporal.emitted, 1u);
  CHECK_EQ(state.temporal.no_output, 1u);
  CHECK(spyro_paired_temporal_proven(state.temporal));
  CHECK(std::equal(ramBefore.begin(), ramBefore.end(), std::begin(game->core.ram)));
  CHECK(std::equal(scratchBefore.begin(), scratchBefore.end(), std::begin(game->core.scratch)));
  checkSourceUnchanged(previousBefore, state.previous);
  checkSourceUnchanged(currentBefore, state.current);
  CHECK_EQ(game->rq.n, 0);
  CHECK(game->rqRedirect == nullptr);
}

void test_temporal_evidence_requires_complete_visible_observation() {
  CHECK(spyro_paired_temporal_selftest());
  auto game = std::make_unique<Game>();
  SpyroContext context;
  game->core.gameCtx = &context;
  auto &evidence = context.pairedActor.temporal;
  CHECK(spyro_paired_temporal_complete(evidence));
  evidence = {.calls = 2,
              .midpoint_calls = 1,
              .endpoint_calls = 1,
              .no_output = 2,
              .eligibility_checks = 1,
              .eligible_intervals = 1};
  CHECK(spyro_paired_temporal_complete(evidence));
  CHECK(!spyro_paired_temporal_proven(evidence));
  spyro_paired_actor_temporal_finish(&game->core); // complete empty output is valid normal gameplay
}

void test_forced_endpoint_diagnostics_account_for_both_slots_without_motion_proof() {
  for (const int forcedInterpolation : {0, 1}) {
    auto game = std::make_unique<Game>();
    SpyroContext context;
    game->core.gameCtx = &context;
    game->mods.fps60 = true;
    auto &state = context.pairedActor;
    state.previous = triangle();
    state.current = state.previous;
    state.current.transform.layer_cr[0][5] = 100u;
    state.endpoints_compatible = true;
    state.was_fps60_active = true;
    spyro_temporal_scene_prepare(game->core);
    CHECK(state.temporal_eligible);
    Fps60 presentation(*game, spyro_temporal_scene_source());
    presentation.presentPass(&game->core, static_cast<float>(forcedInterpolation), {});
    CHECK_EQ(state.temporal.endpoint_calls, 1u);
    CHECK(!spyro_paired_temporal_complete(state.temporal, forcedInterpolation));
    presentation.presentPass(&game->core, 1.0f, {});
    CHECK_EQ(state.temporal.endpoint_calls, 2u);
    CHECK_EQ(state.temporal.midpoint_calls, 0u);
    CHECK_EQ(state.temporal.emitted, 2u);
    CHECK(spyro_paired_temporal_complete(state.temporal, forcedInterpolation));
    CHECK(!spyro_paired_temporal_proven(state.temporal));
    // Without the explicit endpoint override, two endpoint callbacks remain incomplete.
    for (const int midpointMode : {-1, 2, -2}) {
      CHECK(!spyro_paired_temporal_complete(state.temporal, midpointMode));
    }
    ++state.temporal.no_output;
    CHECK(!spyro_paired_temporal_complete(state.temporal, forcedInterpolation));
  }
  const SpyroPairedTemporalEvidence midpoint{.calls = 2,
                                             .midpoint_calls = 1,
                                             .endpoint_calls = 1,
                                             .emitted = 2,
                                             .eligibility_checks = 1,
                                             .eligible_intervals = 1};
  for (const int midpointMode : {-1, 2, -2}) {
    CHECK(spyro_paired_temporal_complete(midpoint, midpointMode));
  }
  CHECK(!spyro_paired_temporal_complete(midpoint, 0));
  CHECK(!spyro_paired_temporal_complete(midpoint, 1));
}

void test_coalesced_actor_buckets_merge_with_field_world() {
  auto game = std::make_unique<Game>();
  SpyroContext context;
  game->core.gameCtx = &context;
  game->mods.fps60 = true;
  auto frame = triangle();
  setSourceDepth(frame, 1000, 1);
  frame.pose.clear();
  frame.primitives.clear();
  frame.materials.clear();
  // Deliberately shuffled source order, including an equal-bucket FIFO pair and empty chunks.
  const std::array<uint32_t, 8> bins{2, 130, 114, 130, 115, 98, 19, 18};
  for (size_t i = 0; i < bins.size(); ++i) {
    frame.materials.push_back(static_cast<uint32_t>(i + 1u));
    const int32_t localZ = static_cast<int32_t>(bins[i] * 8u) - 1000;
    frame.pose.push_back({localZ, -10, -10});
    frame.pose.push_back({localZ, 10, -10});
    frame.pose.push_back({localZ, 0, 10});
    spyro::paired_actor::Primitive primitive{};
    primitive.two_sided = true;
    primitive.source_ordinal = static_cast<uint32_t>(i);
    for (uint32_t vertex = 0; vertex < 3; ++vertex) {
      primitive.projected_offset[vertex] = static_cast<uint16_t>((i * 3 + vertex) * 4);
      primitive.material_offset[vertex] = static_cast<uint16_t>(i * 4);
    }
    frame.primitives.push_back(primitive);
  }
  frame.layer_counts[0] = static_cast<uint32_t>(frame.pose.size());
  CHECK(spyro_paired_actor_rebuild_endpoint(&game->core, game->rq, frame) ==
        SpyroPairedRebuildResult::Emitted);
  CHECK_EQ(game->rq.n, bins.size());
  if (game->rq.n != static_cast<int>(bins.size())) {
    return;
  }
  const std::array<uint16_t, 8> expected{7, 7, 7, 6, 5, 1, 0, 0};
  const std::array<uint8_t, 8> sourceColors{2, 4, 5, 3, 6, 7, 8, 1};
  for (size_t i = 0; i < expected.size(); ++i) {
    CHECK_EQ(game->rq.items[i].painter_replay.key.ot_bin, expected[i]);
    CHECK_EQ(game->rq.items[i].rs[0], sourceColors[i]);
  }
  std::vector<RqItem> captured(game->rq.items, game->rq.items + game->rq.n);
  for (uint16_t bin : {8, 7, 6, 1, 0}) {
    auto world = captured.front();
    world.painter_object = 0x800258F0u;
    world.painter_replay = spyro::scene_painter_order::world(bin, 0, 0);
    world.seq = world.draw_seq = static_cast<uint32_t>(captured.size());
    captured.push_back(world);
  }
  auto &state = context.pairedActor;
  state.previous = state.current = frame;
  state.endpoints_compatible = true;
  state.was_fps60_active = true;
  spyro_temporal_scene_prepare(game->core);
  CHECK(state.temporal_eligible);
  Fps60 presentation(*game, spyro_temporal_scene_source());
  // Negative identifiers denote the world face at that global bin; nonnegative are actor order.
  const std::array<int, 13> replay{-8, -7, 0, 1, 2, -6, 3, 4, -1, 5, -100, 6, 7};
  for (float t : {0.0f, 0.5f, 1.0f}) {
    presentation.presentPass(&game->core, t, {captured, 1});
    CHECK_EQ(presentation.mSink->n, bins.size());
    const auto plan = planPainterItemStream(presentation.mPresentStream);
    CHECK(plan.accepted());
    CHECK_EQ(plan.commands.size(), replay.size());
    if (plan.commands.size() != replay.size()) {
      continue;
    }
    for (size_t i = 0; i < replay.size(); ++i) {
      const auto *item = presentation.mPresentStream[plan.commands[i].item_index];
      if (replay[i] < 0) {
        CHECK_EQ(item->painter_object, 0x800258F0u);
        CHECK_EQ(item->painter_replay.key.ot_bin, replay[i] == -100 ? 0 : -replay[i]);
      } else {
        CHECK(item == &presentation.mSink->items[replay[i]]);
        CHECK_EQ(item->painter_replay.key.ot_bin, expected[replay[i]]);
        CHECK_EQ(item->rs[0], sourceColors[replay[i]]);
      }
    }
  }
  checkSourceUnchanged(frame, state.previous);
  checkSourceUnchanged(frame, state.current);

  // The title front-end retains its isolated actor group: no shared-world coalescing key.
  frame.authored_replay = false;
  game->rq.consumed = 1;
  CHECK(spyro_paired_actor_rebuild_endpoint(&game->core, game->rq, frame) ==
        SpyroPairedRebuildResult::Emitted);
  CHECK_EQ(game->rq.n, bins.size());
  for (int i = 0; i < game->rq.n; ++i) {
    CHECK_EQ(game->rq.items[i].painter_replay.domain, 0u);
  }
}

void test_moving_camera_depth_uses_source_midpoint() {
  auto game = std::make_unique<Game>();
  SpyroContext context;
  game->core.gameCtx = &context;
  game->mods.fps60 = true;
  auto &state = context.pairedActor;
  state.previous = triangle();
  state.current = state.previous;
  setSourceDepth(state.previous, 1023, 1);
  setSourceDepth(state.current, 1281, 1);
  state.endpoints_compatible = true;
  state.was_fps60_active = true;
  spyro_temporal_scene_prepare(game->core);
  CHECK(state.temporal_eligible);
  const auto previous = state.previous, current = state.current;
  Fps60 presentation(*game, spyro_temporal_scene_source());
  for (const auto [t, expectedBin] :
       std::array<std::pair<float, uint16_t>, 3>{{{0, 6}, {0.5f, 8}, {1, 9}}}) {
    presentation.presentPass(&game->core, t, {});
    CHECK_EQ(presentation.mSink->n, 1);
    if (presentation.mSink->n == 1) {
      CHECK_EQ(presentation.mSink->items[0].painter_replay.key.ot_bin, expectedBin);
    }
  }
  checkSourceUnchanged(previous, state.previous);
  checkSourceUnchanged(current, state.current);
  state.current.transform.depth_bias++;
  CHECK(!spyro_paired_actor_fps60_eligible(state));
}

void test_stationary_unequal_depth_faces_preserve_authored_bucket_fifo() {
  for (bool authoredReplay : {true, false}) {
    auto game = std::make_unique<Game>();
    SpyroContext context;
    game->core.gameCtx = &context;
    game->mods.fps60 = true;
    auto frame = triangle();
    frame.authored_replay = authoredReplay;
    setSourceDepth(frame, 1000, 1);
    frame.materials = {1u, 2u};
    // Exact endpoints put A then B into local bucket 125. The continuous keys are
    // 125 and 125.125: sorting those keys reverses a stationary authored FIFO at t=0.5.
    frame.pose = {
        {0, -100, -100}, {0, 100, -100}, {0, 0, 100}, {1, -100, -100}, {1, 100, -100}, {1, 0, 100}};
    frame.layer_counts[0] = 6;
    auto second = frame.primitives.front();
    second.source_ordinal = 1;
    for (size_t i = 0; i < 3; ++i) {
      second.projected_offset[i] += 12;
      second.material_offset[i] = 4;
    }
    frame.primitives.push_back(second);
    auto &state = context.pairedActor;
    state.previous = state.current = frame;
    state.endpoints_compatible = true;
    state.was_fps60_active = true;
    spyro_temporal_scene_prepare(game->core);
    CHECK(state.temporal_eligible);
    Fps60 presentation(*game, spyro_temporal_scene_source());
    for (float t : {0.0f, 0.5f, 1.0f}) {
      presentation.presentPass(&game->core, t, {});
      CHECK_EQ(presentation.mSink->n, 2);
      if (presentation.mSink->n != 2) {
        continue;
      }
      const auto &first = presentation.mSink->items[0];
      const auto &last = presentation.mSink->items[1];
      const bool isolatedMidpoint = !authoredReplay && t == 0.5f;
      CHECK_EQ(first.rs[0], isolatedMidpoint ? 2 : 1);
      CHECK_EQ(last.rs[0], isolatedMidpoint ? 1 : 2);
      CHECK(first.xsf[0] != last.xsf[0]); // distinct depth still affects continuous projection
      if (authoredReplay) {
        CHECK_EQ(first.painter_replay.key.ot_bin, 6);
        CHECK_EQ(last.painter_replay.key.ot_bin, 6);
        const auto plan = planPainterItemStream(presentation.mPresentStream);
        CHECK(plan.accepted());
        CHECK_EQ(plan.commands.size(), 2u);
        if (plan.commands.size() == 2) {
          CHECK(presentation.mPresentStream[plan.commands[0].item_index] == &first);
          CHECK(presentation.mPresentStream[plan.commands[1].item_index] == &last);
        }
      } else {
        CHECK_EQ(first.painter_replay.domain, 0u);
        CHECK_EQ(last.painter_replay.domain, 0u);
      }
    }
    checkSourceUnchanged(frame, state.previous);
    checkSourceUnchanged(frame, state.current);
  }
}

void test_stationary_fractional_source_preserves_every_presentation() {
  for (const int32_t cameraDepth : {1000, 40000}) {
    auto game = std::make_unique<Game>();
    SpyroContext context;
    game->core.gameCtx = &context;
    game->mods.fps60 = true;
    auto frame = triangle();
    setSourceDepth(frame, cameraDepth);
    // A fractional fixed-point transform exposes precision lost by integer IR/denominator
    // conversion. The far case independently distinguishes signed IR3 from projection depth.
    frame.transform.layer_cr[0][0] = 4095u;
    frame.transform.layer_cr[0][2] = 4095u;
    frame.transform.layer_cr[0][4] = 4095u;
    for (auto &vertex : frame.pose) {
      vertex[0] = 1;
    }
    auto &state = context.pairedActor;
    state.previous = state.current = frame;
    state.endpoints_compatible = true;
    state.was_fps60_active = true;
    CHECK(spyro_paired_actor_rebuild_endpoint(&game->core, game->rq, frame) ==
          SpyroPairedRebuildResult::Emitted);
    CHECK_EQ(game->rq.n, 1);
    if (game->rq.n != 1) {
      continue;
    }
    const RqItem endpoint = game->rq.items[0];
    spyro_temporal_scene_prepare(game->core);
    CHECK(state.temporal_eligible);
    Fps60 presentation(*game, spyro_temporal_scene_source());
    for (float t : {0.0f, 0.5f, 1.0f}) {
      presentation.presentPass(&game->core, t, {});
      CHECK_EQ(presentation.mSink->n, 1);
      if (presentation.mSink->n != 1) {
        continue;
      }
      const auto &sample = presentation.mSink->items[0];
      for (size_t vertex = 0; vertex < 3; ++vertex) {
        CHECK_EQ(sample.xsf[vertex], endpoint.xsf[vertex]);
        CHECK_EQ(sample.ysf[vertex], endpoint.ysf[vertex]);
        CHECK_EQ(sample.depth[vertex], endpoint.depth[vertex]);
      }
    }
  }
}

} // namespace

int main() {
  RUN(exact_producer_membership);
  RUN(runtime_factory_rotates_own_endpoints);
  RUN(disabled_and_discontinuous_frames_refuse);
  RUN(mixed_scene_reconstructs_authored_motion_without_guest_writes);
  RUN(valid_empty_endpoint_keeps_visible_midpoint);
  RUN(temporal_evidence_requires_complete_visible_observation);
  RUN(forced_endpoint_diagnostics_account_for_both_slots_without_motion_proof);
  RUN(coalesced_actor_buckets_merge_with_field_world);
  RUN(moving_camera_depth_uses_source_midpoint);
  RUN(stationary_unequal_depth_faces_preserve_authored_bucket_fifo);
  RUN(stationary_fractional_source_preserves_every_presentation);
  return pt_summary();
}
