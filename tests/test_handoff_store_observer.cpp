#include "core.h"
#include "game.h"
#include "game_runtime.h"
#include "guest_globals.h"
#include "handoff_store_observer.h"
#include "image_identity.h"
#include "lightrec_executor.h"
#include "spyro1_field_scheduler.h"
#include "spyro_context.h"
#include "testutil.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

namespace {

class SyntheticRuntime final : public GameRuntime {
public:
  void *createContext(Core &) override {
    return nullptr;
  }
  void destroyContext(void *) override {}
  void registerOverrides(Game &) override {}
  void bootInit(Core &) override {}
  RenderCapabilities renderCapabilities() const override {
    return RenderCapabilities::direct();
  }
  bool guestVramIsPicture(const Game &) const override {
    return false;
  }
};

constexpr std::uint32_t kReset = 0x80013690u;
constexpr std::uint32_t kLoader = 0x80013B44u;
constexpr std::uint32_t kPadVsync = 0x80053C68u;
constexpr std::uint32_t kTick = 0x80033A68u;
constexpr std::uint32_t kReturn = 0x80070000u;
using spyro::guest::kGamestate;
constexpr std::uint32_t kLevelTick = 0x800758C8u;
constexpr std::uint32_t kGameTick = 0x8007572Cu;

void writeCode(Core &core) {
  // Synthetic stores at the four selected PCs, without copying the retail instruction corpus.
  constexpr std::array reset{
      0x3C088007u, // lui t0,0x8007
      0x00000000u, // nop
      0xAD0058C8u, // sw zero,0x58c8(t0)
      0x00000000u, // nop (the console's adjacent PC is not a store)
      0xAD00572Cu, // sw zero,0x572c(t0)
      0x03E00008u, // jr ra
      0x00000000u, // delay-slot nop
  };
  constexpr std::array loader{
      0x3C088007u, // lui t0,0x8007
      0x00000000u,
      0xAD0057D8u, // sw zero,0x57d8(t0)
      0x03E00008u,
      0x00000000u,
  };
  constexpr std::array tick{
      0x25290001u, // addiu t1,t1,1
      0xAD09572Cu, // sw t1,0x572c(t0)
      0x03E00008u,
      0x00000000u,
  };
  constexpr std::array padVsync{
      0x3C018007u, // lui at,0x8007: 0x80053C68
      0x8C2258C8u, // lw v0,0x58c8(at): 0x80053C6C
      0x00000000u, // nop: 0x80053C70
      0x00000000u, // nop: 0x80053C74
      0x00000000u, // nop: 0x80053C78
      0x00000000u, // nop: 0x80053C7C
      0x00000000u, // nop: 0x80053C80
      0x00000000u, // nop: 0x80053C84
      0x24420001u, // addiu v0,v0,1: 0x80053C88
      0x3C018007u, // lui at,0x8007: 0x80053C8C
      0xAC2258C8u, // sw v0,0x58c8(at): selected PC 0x80053C90
      0x03E00008u, // jr ra: 0x80053C94
      0x00000000u, // delay slot: 0x80053C98
  };
  for (std::size_t index = 0; index < reset.size(); ++index) {
    core.mem_w32(kReset + static_cast<std::uint32_t>(index * 4u), reset[index]);
  }
  for (std::size_t index = 0; index < loader.size(); ++index) {
    core.mem_w32(kLoader + static_cast<std::uint32_t>(index * 4u), loader[index]);
  }
  for (std::size_t index = 0; index < tick.size(); ++index) {
    core.mem_w32(kTick + static_cast<std::uint32_t>(index * 4u), tick[index]);
  }
  for (std::size_t index = 0; index < padVsync.size(); ++index) {
    core.mem_w32(kPadVsync + static_cast<std::uint32_t>(index * 4u), padVsync[index]);
  }
  core.imageCatalog().activate("synthetic handoff stores", {0x10000u, 0x75800u}, 1u);
  core.r[31] = kReturn;
}

void runStore(Core &core, std::uint32_t entry) {
  const auto result = core.lightrecExecutor().executeFunction(
      entry, kReturn, psx::cpu::ExecutionBudget::fromCycles(200u));
  CHECK_EQ(result.reason, psx::cpu::ExecutionExitReason::GuestReturn);
}

void runHandoff(Core &core) {
  for (const std::uint32_t entry : {kReset, kLoader}) {
    runStore(core, entry);
  }
  core.r[9] = core.mem_r32(kGameTick);
  runStore(core, kTick);
}

void enableSyntheticFields(Game &game, SyntheticRuntime &runtime, SpyroContext &context) {
  game.core.gameCtx = &context;
  game.runtime = &runtime;
  game.gpu_dev.s_gpu_on = 0;
}

void resetWords(Core &core) {
  core.mem_w32(kGamestate, 13u);
  core.mem_w32(kLevelTick, 5326u);
  core.mem_w32(kGameTick, 7u);
  core.r[8] = 0x80070000u;
  core.r[9] = 7u;
}

void test_shipping_observer_captures_four_stores_and_unreachable_control() {
  SyntheticRuntime runtime;
  psxport_install_game(runtime);
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  spyro1::FieldScheduler fields(*game);
  writeCode(core);
  resetWords(core);
  runHandoff(core); // Warm the ordinary shipping JIT path before the observer is armed.
  const auto plainStage = core.mem_r32(kGamestate);
  const auto plainLevelTick = core.mem_r32(kLevelTick);
  const auto plainGameTick = core.mem_r32(kGameTick);

  resetWords(core);
  spyro1::HandoffStoreObserver observer(true, fields);
  CHECK_EQ(observer.arm(core), psx::cpu::StoreObserverStatus::Configured);
  runHandoff(core);
  const auto counts = observer.counts();
  const auto samples = observer.samples();
  CHECK_EQ(counts.targetCount, spyro1::kHandoffStoreTargets.size());
  CHECK_EQ(samples.size(), 4u);
  CHECK_EQ(observer.omitted(), 0u);
  for (std::size_t index = 0; index < 4u; ++index) {
    const auto targetIndex = index == 3u ? 4u : index;
    CHECK_EQ(counts.targets[targetIndex].guestPc, spyro1::kHandoffStoreTargets[targetIndex]);
    CHECK_EQ(counts.targets[targetIndex].before, 1u);
    CHECK_EQ(counts.targets[targetIndex].after, 1u);
    CHECK_EQ(samples[index].guestPc, spyro1::kHandoffStoreTargets[targetIndex]);
    CHECK(samples[index].after.cycle > samples[index].before.cycle);
  }
  CHECK_EQ(counts.targets[3].guestPc, 0x80053C90u);
  CHECK_EQ(counts.targets[3].before, 0u);
  CHECK_EQ(counts.targets[3].after, 0u);
  CHECK_EQ(counts.targets[5].guestPc, 0xFFFFFFFCu);
  CHECK_EQ(counts.targets[5].before, 0u);
  CHECK_EQ(counts.targets[5].after, 0u);
  CHECK(counts.executedJitInstructions > 0u);
  CHECK_EQ(counts.fallbackInstructions, 0u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Transition).hits, 3u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Neighborhood).hits, 1u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Bracket).hits, 0u);
  CHECK(observer.bracketOpened());
  CHECK(observer.bracketClosed());
  CHECK_EQ(samples[0].before.address, kLevelTick);
  CHECK_EQ(samples[0].before.word, 5326u);
  CHECK_EQ(samples[0].after.word, 0u);
  CHECK_EQ(samples[1].before.address, kGameTick);
  CHECK_EQ(samples[1].before.word, 7u);
  CHECK_EQ(samples[1].after.word, 0u);
  CHECK_EQ(samples[2].before.address, kGamestate);
  CHECK_EQ(samples[2].before.stage, 13u);
  CHECK_EQ(samples[2].after.stage, 0u);
  CHECK_EQ(samples[3].before.address, kGameTick);
  CHECK_EQ(samples[3].before.gameTick, 0u);
  CHECK_EQ(samples[3].after.gameTick, 1u);
  CHECK_EQ(core.mem_r32(kGamestate), plainStage);
  CHECK_EQ(core.mem_r32(kLevelTick), plainLevelTick);
  CHECK_EQ(core.mem_r32(kGameTick), plainGameTick);
  observer.finish();
  observer.report();

  resetWords(core);
  spyro1::HandoffStoreObserver disabled(false, fields);
  CHECK_EQ(disabled.arm(core), psx::cpu::StoreObserverStatus::Configured);
  runHandoff(core);
  CHECK(disabled.samples().empty());
  CHECK_EQ(core.mem_r32(kGameTick), plainGameTick);
}

void test_late_reset_survives_more_than_64_routine_ticks() {
  SyntheticRuntime runtime;
  psxport_install_game(runtime);
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  spyro1::FieldScheduler fields(*game);
  writeCode(core);
  resetWords(core);
  spyro1::HandoffStoreObserver observer(true, fields);
  CHECK_EQ(observer.arm(core), psx::cpu::StoreObserverStatus::Configured);
  for (std::uint32_t index = 0; index < 80u; ++index) {
    runStore(core, kTick);
  }
  CHECK_EQ(core.mem_r32(kGameTick), 87u);
  runHandoff(core);
  const auto samples = observer.samples();
  const auto routine = observer.classCounts(spyro1::HandoffStoreClass::Routine);
  const auto transition = observer.classCounts(spyro1::HandoffStoreClass::Transition);
  const auto neighborhood = observer.classCounts(spyro1::HandoffStoreClass::Neighborhood);
  CHECK_EQ(routine.hits, 80u);
  CHECK_EQ(routine.recorded, 8u);
  CHECK_EQ(routine.omitted, 72u);
  CHECK_EQ(transition.hits, 3u);
  CHECK_EQ(transition.recorded, 3u);
  CHECK_EQ(transition.omitted, 0u);
  CHECK_EQ(neighborhood.hits, 1u);
  CHECK_EQ(neighborhood.recorded, 1u);
  CHECK_EQ(neighborhood.omitted, 0u);
  CHECK_EQ(observer.omitted(), 72u);
  CHECK_EQ(samples.size(), 12u);
  CHECK_EQ(samples[8].guestPc, 0x80013698u);
  CHECK_EQ(samples[8].ordinal, 81u);
  CHECK_EQ(samples[8].before.levelTick, 5326u);
  CHECK_EQ(samples[8].after.levelTick, 0u);
  CHECK_EQ(samples[9].guestPc, 0x800136A0u);
  CHECK_EQ(samples[9].before.gameTick, 87u);
  CHECK_EQ(samples[9].after.gameTick, 0u);
  CHECK_EQ(samples[10].guestPc, 0x80013B4Cu);
  CHECK_EQ(samples[10].before.stage, 13u);
  CHECK_EQ(samples[10].after.stage, 0u);
  CHECK_EQ(samples[11].guestPc, 0x80033A6Cu);
  CHECK_EQ(samples[11].sampleClass, spyro1::HandoffStoreClass::Neighborhood);
  CHECK_EQ(samples[11].before.gameTick, 0u);
  CHECK_EQ(samples[11].after.gameTick, 1u);
  const auto counts = observer.counts();
  CHECK_EQ(counts.targets[3].before, 0u);
  CHECK_EQ(counts.targets[3].after, 0u);
  CHECK_EQ(counts.targets[4].before, 81u);
  CHECK_EQ(counts.targets[4].after, 81u);
  CHECK_EQ(counts.targets[5].before, 0u);
  CHECK_EQ(counts.targets[5].after, 0u);
  CHECK_EQ(counts.fallbackInstructions, 0u);
  observer.finish();
  observer.report();
}

void test_padv_sync_bracket_keeps_callback_origin_after_boring_prefix() {
  SyntheticRuntime runtime;
  psxport_install_game(runtime);
  auto game = std::make_unique<Game>();
  SpyroContext context;
  enableSyntheticFields(*game, runtime, context);
  Core &core = game->core;
  spyro1::FieldScheduler fields(*game);
  writeCode(core);
  resetWords(core);
  fields.observeVblankCallback(kPadVsync);
  spyro1::HandoffStoreObserver observer(true, fields);
  CHECK_EQ(observer.arm(core), psx::cpu::StoreObserverStatus::Configured);

  for (std::uint32_t index = 0; index < 80u; ++index) {
    runStore(core, kPadVsync);
  }
  CHECK_EQ(core.mem_r32(kLevelTick), 5406u);
  runStore(core, kReset);
  runStore(core, kLoader);
  for (std::uint32_t index = 0; index < 3u; ++index) {
    CHECK(fields.deliver({"synthetic-handoff-field", false, false}));
  }
  core.r[9] = core.mem_r32(kGameTick);
  runStore(core, kTick);
  CHECK(fields.deliver({"synthetic-post-tick-field", false, false}));
  CHECK(fields.activeDeliverySite().empty());
  CHECK_EQ(core.mem_r32(kLevelTick), 4u);
  CHECK_EQ(core.mem_r32(kGameTick), 1u);

  const auto samples = observer.samples();
  const auto counts = observer.counts();
  CHECK(observer.bracketOpened());
  CHECK(observer.bracketClosed());
  CHECK_EQ(samples.size(), 15u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Transition).hits, 3u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Bracket).hits, 3u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Bracket).recorded, 3u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Bracket).omitted, 0u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Neighborhood).hits, 1u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Routine).hits, 81u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Routine).recorded, 8u);
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Routine).omitted, 73u);
  CHECK_EQ(observer.omitted(), 73u);
  CHECK_EQ(samples[0].deliverySite[0], '\0');
  CHECK_EQ(samples[10].guestPc, 0x80013B4Cu);
  CHECK_EQ(samples[10].after.stage, 0u);
  for (std::size_t index = 0; index < 3u; ++index) {
    const auto &sample = samples[11u + index];
    CHECK_EQ(sample.guestPc, 0x80053C90u);
    CHECK_EQ(sample.sampleClass, spyro1::HandoffStoreClass::Bracket);
    CHECK_EQ(sample.before.levelTick, index);
    CHECK_EQ(sample.after.levelTick, index + 1u);
    CHECK(std::string_view(sample.deliverySite.data()) == "synthetic-handoff-field");
  }
  CHECK_EQ(samples[14].guestPc, 0x80033A6Cu);
  CHECK_EQ(samples[14].before.levelTick, 3u);
  CHECK_EQ(samples[14].before.gameTick, 0u);
  CHECK_EQ(samples[14].after.gameTick, 1u);
  CHECK_EQ(counts.targets[0].before, 1u);
  CHECK_EQ(counts.targets[1].before, 1u);
  CHECK_EQ(counts.targets[2].before, 1u);
  CHECK_EQ(counts.targets[3].before, 84u);
  CHECK_EQ(counts.targets[3].after, 84u);
  CHECK_EQ(counts.targets[4].before, 1u);
  CHECK_EQ(counts.targets[4].after, 1u);
  CHECK_EQ(counts.targets[5].before, 0u);
  CHECK_EQ(counts.targets[5].after, 0u);
  CHECK(counts.executedJitInstructions > 0u);
  CHECK_EQ(counts.fallbackInstructions, 0u);
  observer.finish();

  resetWords(core);
  spyro1::HandoffStoreObserver disabled(false, fields);
  CHECK_EQ(disabled.arm(core), psx::cpu::StoreObserverStatus::Configured);
  runStore(core, kReset);
  runStore(core, kLoader);
  for (std::uint32_t index = 0; index < 3u; ++index) {
    CHECK(fields.deliver({"synthetic-handoff-field", false, false}));
  }
  core.r[9] = core.mem_r32(kGameTick);
  runStore(core, kTick);
  CHECK_EQ(core.mem_r32(kLevelTick), 3u);
  CHECK_EQ(core.mem_r32(kGameTick), 1u);
  CHECK(disabled.samples().empty());
}

void test_unreached_stage_reports_no_padv_sync_bracket() {
  SyntheticRuntime runtime;
  psxport_install_game(runtime);
  auto game = std::make_unique<Game>();
  SpyroContext context;
  enableSyntheticFields(*game, runtime, context);
  Core &core = game->core;
  spyro1::FieldScheduler fields(*game);
  writeCode(core);
  resetWords(core);
  fields.observeVblankCallback(kPadVsync);
  spyro1::HandoffStoreObserver observer(true, fields);
  CHECK_EQ(observer.arm(core), psx::cpu::StoreObserverStatus::Configured);
  CHECK(fields.deliver({"synthetic-unreached-stage", false, false}));
  core.r[9] = core.mem_r32(kGameTick);
  runStore(core, kTick);

  const auto counts = observer.counts();
  const auto samples = observer.samples();
  CHECK(!observer.bracketOpened());
  CHECK(!observer.bracketClosed());
  CHECK_EQ(observer.classCounts(spyro1::HandoffStoreClass::Bracket).hits, 0u);
  CHECK_EQ(samples.size(), 2u);
  CHECK_EQ(samples[0].guestPc, 0x80053C90u);
  CHECK_EQ(samples[0].sampleClass, spyro1::HandoffStoreClass::Routine);
  CHECK(std::string_view(samples[0].deliverySite.data()) == "synthetic-unreached-stage");
  CHECK_EQ(samples[1].guestPc, 0x80033A6Cu);
  CHECK_EQ(counts.targets[2].before, 0u);
  CHECK_EQ(counts.targets[3].before, 1u);
  CHECK_EQ(counts.targets[3].after, 1u);
  CHECK_EQ(counts.targets[4].before, 1u);
  CHECK_EQ(counts.targets[4].after, 1u);
  CHECK_EQ(counts.targets[5].before, 0u);
  CHECK_EQ(counts.targets[5].after, 0u);
  CHECK(counts.executedJitInstructions > 0u);
  CHECK_EQ(counts.fallbackInstructions, 0u);
  observer.finish();
}

} // namespace

int main() {
  RUN(shipping_observer_captures_four_stores_and_unreachable_control);
  RUN(late_reset_survives_more_than_64_routine_ticks);
  RUN(padv_sync_bracket_keeps_callback_origin_after_boring_prefix);
  RUN(unreached_stage_reports_no_padv_sync_bracket);
  return pt_summary();
}
