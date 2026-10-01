#include "core.h"
#include "demo_text_scene.h"
#include "guest_globals.h"
#include "hud_text_builder.h"
#include "testutil.h"

#include <memory>
#include <vector>

namespace {

constexpr std::uint32_t kHudMobyCursor = 0x80075710u;
constexpr std::uint32_t kCosine = 0x8006CC78u;
constexpr std::uint32_t kArenaEnd = 0x80120000u;
constexpr std::uint32_t kMobySize = 0x58u;
constexpr std::uint32_t kQueue = spyro::hud_text::kShadedMobyQueue;

struct Harness {
  std::unique_ptr<Core> core = std::make_unique<Core>();

  Harness() {
    core->mem_w32(kHudMobyCursor, kArenaEnd);
    core->mem_w32(spyro::guest::kDemoMode, 1u);
    core->mem_w32(spyro::guest::kLevelTicks, 3u);
    for (std::uint32_t i = 0; i < 256u; ++i) {
      core->mem_w16(kCosine + i * 2u, (std::uint16_t)(i * 256u)); // cos[i] >> 7 == i * 2
    }
  }

  std::vector<std::uint32_t> queue() const {
    std::vector<std::uint32_t> out;
    for (std::uint32_t slot = 0; slot < 256u && core->mem_r32(kQueue + slot * 4u) != 0u; ++slot) {
      out.push_back(core->mem_r32(kQueue + slot * 4u));
    }
    return out;
  }
};

// The guest's own call at 0x8001F000 is gated on g_DemoMode, so a frame outside the demo plans and
// builds nothing.
void test_outside_the_demo_nothing_is_planned_or_written() {
  CHECK(!spyro::demo_text_scene::plan(0u).armed);
  Harness h;
  h.core->mem_w32(spyro::guest::kDemoMode, 0u);
  CHECK(spyro::demo_text_scene::submit(h.core.get()));
  CHECK(h.core->mem_r32(kHudMobyCursor) == kArenaEnd);
  CHECK(h.queue().empty());
}

// The bytes of 0x80018908: eight glyphs (the space adds no Moby), the caption builder's position,
// spacing and space width, and shade 2. The first glyph is full width at (199, 200, 4352); every
// later letter follows a letter, so it is narrow: y + 1 and z = 5120.
void test_the_plan_is_the_guests_caption() {
  const auto plan = spyro::demo_text_scene::plan(1u);
  CHECK(plan.armed);
  CHECK(plan.shadeIndex == 2u);
  CHECK(plan.layout.glyphs.size() == 8u);
  const auto &first = plan.layout.glyphs[0];
  CHECK(first.mobyClass == 429u); // 'D'
  CHECK(first.position.x == 199 && first.position.y == 200 && first.position.z == 4352);
  const auto &second = plan.layout.glyphs[1];
  CHECK(second.mobyClass == 430u); // 'E'
  CHECK(second.position.x == 199 + 18);
  CHECK(second.position.y == 201 && second.position.z == 5120);
  // 'M' follows 'E' (narrow, advance 16), 'O' follows, then the space adds (16 * 3) >> 2 = 12 and
  // resets the run, so the second word starts full width at y = 200.
  const auto &m = plan.layout.glyphs[4];
  CHECK(m.mobyClass == 438u); // 'M' of the second word
  CHECK(m.position.y == 200 && m.position.z == 4352);
}

void test_submit_builds_the_arena_wobbles_and_queues_newest_first() {
  Harness h;
  CHECK(spyro::demo_text_scene::submit(h.core.get()));
  CHECK(h.core->mem_r32(kHudMobyCursor) == kArenaEnd - 8u * kMobySize);
  const auto queued = h.queue();
  CHECK(queued.size() == 8u);
  // The queue runs from the cursor up: the LAST glyph built is the first queued.
  CHECK(queued.front() == kArenaEnd - 8u * kMobySize);
  CHECK(queued.back() == kArenaEnd - kMobySize);
  CHECK(h.core->mem_r16(queued.back() + 0x36u) == 429u); // 'D' was built first
  // Glyph i (in build order) takes COSINE_8((g_LevelTicks * 4 + i * 12) & 0xFF) >> 7.
  for (std::uint32_t i = 0; i < 8u; ++i) {
    const std::uint32_t glyph = kArenaEnd - (i + 1u) * kMobySize;
    const std::uint32_t phase = (3u * 4u + i * 12u) & 0xFFu;
    CHECK(h.core->mem_r8(glyph + 0x46u) == (std::uint8_t)(phase * 2u));
    CHECK(h.core->mem_r8(glyph + 0x50u) == 0xFFu); // the screen-space path's bit
  }
}

// Both refusals are atomic: a full arena and a full queue each leave every word as it was.
void test_a_full_arena_or_queue_refuses_without_writing() {
  Harness arena;
  arena.core->mem_w32(kHudMobyCursor, 0x80010000u + kMobySize);
  CHECK(!spyro::demo_text_scene::submit(arena.core.get()));
  CHECK(arena.core->mem_r32(kHudMobyCursor) == 0x80010000u + kMobySize);
  CHECK(arena.queue().empty());

  Harness queue;
  for (std::uint32_t slot = 0; slot < 252u; ++slot) {
    queue.core->mem_w32(kQueue + slot * 4u, 0x80080000u + slot * 4u);
  }
  CHECK(!spyro::demo_text_scene::submit(queue.core.get()));
  CHECK(queue.core->mem_r32(kHudMobyCursor) == kArenaEnd);
  CHECK(queue.queue().size() == 252u);
}

// The caption is appended after whatever the queue already holds, and the list stays terminated.
void test_the_caption_follows_the_existing_queue_entries() {
  Harness h;
  h.core->mem_w32(kQueue, 0x80077FECu);
  h.core->mem_w32(kQueue + 4u, 0x80078044u);
  h.core->mem_w32(kQueue + 8u, 0u);
  h.core->mem_w32(kQueue + 12u, 0xDEADBEEFu); // a stale entry past the terminator
  CHECK(spyro::demo_text_scene::submit(h.core.get()));
  const auto queued = h.queue();
  CHECK(queued.size() == 10u);
  CHECK(queued[0] == 0x80077FECu && queued[1] == 0x80078044u);
  CHECK(h.core->mem_r32(kQueue + 10u * 4u) == 0u);
}

} // namespace

int main() {
  RUN(outside_the_demo_nothing_is_planned_or_written);
  RUN(the_plan_is_the_guests_caption);
  RUN(submit_builds_the_arena_wobbles_and_queues_newest_first);
  RUN(a_full_arena_or_queue_refuses_without_writing);
  RUN(the_caption_follows_the_existing_queue_entries);
  return pt_summary();
}
