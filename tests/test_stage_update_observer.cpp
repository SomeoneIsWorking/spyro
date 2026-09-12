#include "core.h"
#include "stage_update_observer.h"
#include "testutil.h"

namespace {

constexpr std::uint32_t kStageUpdate = 0x8003385Cu;

void test_reached_and_unreachable_returns_use_the_same_sampler() {
  Core core;
  core.mem_w32(0x800757D8u, 0u);
  core.mem_w32(0x800758C8u, 1u);
  core.mem_w32(0x8007572Cu, 1u);
  core.mem_w32(0x80076E28u, 0x80000010u);
  core.mem_w32(0x80076E90u, 0x80000010u);
  core.mem_w32(0x80078A58u, 84992u);
  core.mem_w32(0x80078A5Cu, 47125u);
  core.mem_w32(0x80078A60u, 9570u);

  spyro1::StageUpdateObserver reached(true, kStageUpdate);
  spyro1::StageUpdateObserver unreachable(true, 0xFFFFFFFCu);
  spyro1::StageUpdateObserver disabled(false, kStageUpdate);
  reached.afterReturn(core, kStageUpdate);
  unreachable.afterReturn(core, kStageUpdate);
  disabled.afterReturn(core, kStageUpdate);

  CHECK_EQ(reached.scanned(), 1u);
  CHECK_EQ(reached.matched(), 1u);
  CHECK_EQ(reached.gameplay(), 1u);
  CHECK_EQ(reached.samples().size(), 1u);
  CHECK_EQ(reached.samples()[0].gameTick, 1u);
  CHECK_EQ(reached.samples()[0].cameraState, 0x80000010u);
  CHECK_EQ(reached.samples()[0].player[1], 47125);
  CHECK_EQ(unreachable.scanned(), 1u);
  CHECK_EQ(unreachable.matched(), 0u);
  CHECK_EQ(unreachable.gameplay(), 0u);
  CHECK(unreachable.samples().empty());
  CHECK_EQ(disabled.scanned(), 0u);
  CHECK(disabled.samples().empty());

  reached.report();
  unreachable.report();
  disabled.report();
}

void test_non_gameplay_returns_are_counted_without_samples() {
  Core core;
  core.mem_w32(0x800757D8u, 13u);
  spyro1::StageUpdateObserver observed(true, kStageUpdate);
  observed.afterReturn(core, kStageUpdate);
  CHECK_EQ(observed.scanned(), 1u);
  CHECK_EQ(observed.matched(), 1u);
  CHECK_EQ(observed.gameplay(), 0u);
  CHECK(observed.samples().empty());
  observed.report();
}

} // namespace

int main() {
  RUN(reached_and_unreachable_returns_use_the_same_sampler);
  RUN(non_gameplay_returns_are_counted_without_samples);
  return pt_summary();
}
