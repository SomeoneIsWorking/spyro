#include "core.h"
#include "game.h"
#include "gte_state.h"
#include "hw_bind.h"
#include "stage_update_observer.h"
#include "testutil.h"

#include <memory>

namespace {

constexpr std::uint32_t kStageUpdate = 0x8003385Cu;
constexpr std::uint32_t kProjectionRtps = 0x4A180001u;

void test_reached_and_unreachable_returns_use_the_same_sampler() {
  Core core;
  core.mem_w32(0x800757D8u, 0u);
  core.mem_w32(0x800758C8u, 1u);
  core.mem_w32(0x8007572Cu, 1u);
  core.mem_w32(0x80076E28u, 0x80000010u);
  core.mem_w32(0x80076E90u, 0x80000010u);
  core.mem_w32(0x80075914u, 0x52u);
  core.mem_w32(0x8007592Cu, 1u);
  core.mem_w32(0x80078BECu, 2u);
  core.mem_w32(0x80076EC0u, 3u);
  core.mem_w32(0x80075938u, 45u);
  core.mem_w32(0x80078A58u, 84992u);
  core.mem_w32(0x80078A5Cu, 47125u);
  core.mem_w32(0x80078A60u, 9570u);

  spyro1::StageUpdateObserver reached(true, kStageUpdate);
  spyro1::StageUpdateObserver unreachable(true, 0xFFFFFFFCu);
  spyro1::StageUpdateObserver disabled(false, kStageUpdate);
  unreachable.beginStage(core, kStageUpdate);
  CHECK(!core.rsub.gtePreOp.armed());
  disabled.beginStage(core, kStageUpdate);
  CHECK(!core.rsub.gtePreOp.armed());
  reached.afterReturn(core, kStageUpdate);
  unreachable.afterReturn(core, kStageUpdate);
  disabled.afterReturn(core, kStageUpdate);

  CHECK_EQ(reached.scanned(), 1u);
  CHECK_EQ(reached.matched(), 1u);
  CHECK_EQ(reached.gameplay(), 1u);
  CHECK_EQ(reached.samples().size(), 1u);
  CHECK_EQ(reached.samples()[0].gameTick, 1u);
  CHECK_EQ(reached.samples()[0].cameraState, 0x80000010u);
  CHECK_EQ(reached.samples()[0].cameraTargetState, 0x80000010u);
  CHECK_EQ(reached.samples()[0].cameraMode, 0x52u);
  CHECK_EQ(reached.samples()[0].lookMode, 1u);
  CHECK_EQ(reached.samples()[0].playerCameraGate, 2u);
  CHECK_EQ(reached.samples()[0].cameraBlock, 3u);
  CHECK_EQ(reached.samples()[0].cameraEntranceTimer, 45u);
  CHECK_EQ(reached.samples()[0].player[1], 47125);
  CHECK_EQ(core.mem_r32(0x80075938u), 45u);
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

void test_projection_reads_live_gte_result_and_rejects_wrong_operands() {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  gte_bind(&core);
  core.mem_w32(0x800757D8u, 0u);
  core.mem_w32(0x80076E90u, 0u);
  core.mem_w32(0x80075914u, 0x52u);
  core.mem_w32(0x8007592Cu, 0u);
  core.mem_w32(0x80078BECu, 0u);
  core.mem_w32(0x80076EC0u, 0u);
  core.mem_w32(0x80078A58u, 66536u); // VZ0 port keeps only signed low 16 bits: 1000.
  core.mem_w32(0x80078A5Cu, 100u);
  core.mem_w32(0x80078A60u, 200u);
  core.mem_w32(0x80076DF8u, 0u);
  core.mem_w32(0x80076DFCu, 0u);
  core.mem_w32(0x80076E00u, 0u);
  for (std::uint32_t index = 0; index < 32u; ++index) {
    gte_write_ctrl(index, 0u);
  }
  const std::array<std::uint32_t, 5> identity{4096u, 0u, 4096u, 0u, 4096u};
  for (std::uint32_t index = 0; index < identity.size(); ++index) {
    core.mem_w32(0x80076DD0u + index * 4u, identity[index]);
    gte_write_ctrl(index, identity[index]);
  }
  gte_write_ctrl(26u, 1000u);
  constexpr std::uint32_t kVector =
      (static_cast<std::uint16_t>(-200) << 16u) | static_cast<std::uint16_t>(-100);
  gte_write_data(0u, kVector);
  gte_write_data(1u, 66536u);

  spyro1::StageUpdateObserver observed(true, kStageUpdate);
  observed.beginStage(core, kStageUpdate);
  CHECK(core.rsub.gtePreOp.armed());
  gte_op_at(&core, kProjectionRtps, 0xFFFFFFFFu);
  const std::uint32_t sxy2 = gte_read_data(14u);
  const std::uint32_t mac3 = gte_read_data(27u);
  observed.afterReturn(core, kStageUpdate);
  CHECK(!core.rsub.gtePreOp.armed());
  CHECK_EQ(observed.samples().size(), 1u);
  CHECK_EQ(observed.samples()[0].gteOps, 1u);
  CHECK_EQ(observed.samples()[0].rtpsOps, 1u);
  CHECK_EQ(observed.samples()[0].projection.candidates, 1u);
  // func_80017AA4 at 0x80017B24..44 stores exactly these converted GTE port values.
  CHECK_EQ(observed.samples()[0].projection.x, static_cast<std::int16_t>(sxy2 & 0xFFFFu));
  CHECK_EQ(observed.samples()[0].projection.y, static_cast<std::int16_t>(sxy2 >> 16u));
  CHECK_EQ(observed.samples()[0].projection.depth, mac3);
  CHECK_EQ(observed.samples()[0].projection.x, -100);
  CHECK_EQ(observed.samples()[0].projection.y, -200);
  CHECK_EQ(observed.samples()[0].projection.depth, 1000u);
  CHECK_EQ(observed.samples()[0].projection.cameraMode, 0x52u);
  CHECK_EQ(observed.samples()[0].projection.lookMode, 0u);
  CHECK_EQ(observed.samples()[0].projection.playerCameraGate, 0u);
  CHECK_EQ(observed.samples()[0].projection.cameraBlock, 0u);
  CHECK_EQ(core.mem_r32(0x80076E90u), 0u);
  CHECK_EQ(core.mem_r32(0x80075914u), 0x52u);

  gte_write_data(0u, kVector ^ 1u); // Positive instruction, deliberately wrong input vector.
  spyro1::StageUpdateObserver unreachable(true, kStageUpdate);
  unreachable.beginStage(core, kStageUpdate);
  gte_op_at(&core, kProjectionRtps, 0x80017B20u);
  unreachable.afterReturn(core, kStageUpdate);
  CHECK_EQ(unreachable.samples().size(), 1u);
  CHECK_EQ(unreachable.samples()[0].gteOps, 1u);
  CHECK_EQ(unreachable.samples()[0].rtpsOps, 1u);
  CHECK_EQ(unreachable.samples()[0].projection.candidates, 0u);

  gte_write_data(0u, kVector);
  gte_write_ctrl(0u, 2048u); // Same RTPS operands, wrong camera matrix.
  spyro1::StageUpdateObserver wrongMatrix(true, kStageUpdate);
  wrongMatrix.beginStage(core, kStageUpdate);
  gte_op_at(&core, kProjectionRtps, 0x80017B20u);
  wrongMatrix.afterReturn(core, kStageUpdate);
  CHECK_EQ(wrongMatrix.samples()[0].gteOps, 1u);
  CHECK_EQ(wrongMatrix.samples()[0].projection.candidates, 0u);

  gte_write_ctrl(0u, identity[0]);
  spyro1::StageUpdateObserver ambiguous(true, kStageUpdate);
  ambiguous.beginStage(core, kStageUpdate);
  gte_op_at(&core, kProjectionRtps, 0x80017B20u);
  gte_op_at(&core, kProjectionRtps, 0x80017B20u);
  ambiguous.afterReturn(core, kStageUpdate);
  CHECK_EQ(ambiguous.samples()[0].gteOps, 2u);
  CHECK_EQ(ambiguous.samples()[0].rtpsOps, 2u);
  CHECK_EQ(ambiguous.samples()[0].projection.candidates, 2u);
  observed.report();
  unreachable.report();
  wrongMatrix.report();
  ambiguous.report();
  GTE_BindState(nullptr);
}

} // namespace

int main() {
  RUN(reached_and_unreachable_returns_use_the_same_sampler);
  RUN(non_gameplay_returns_are_counted_without_samples);
  RUN(projection_reads_live_gte_result_and_rejects_wrong_operands);
  return pt_summary();
}
