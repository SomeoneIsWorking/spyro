#include "config_vars.h"
#include "emulated_time.h"
#include "execution_services.h"
#include "fps60.h"
#include "frame_pacer.h"
#include "game.h"
#include "guest_call.h"
#include "lightrec_executor.h"
#include "native_dispatch.h"
#include "spyro1_field_scheduler.h"
#include "spyro1_runtime.h"
#include "spyro_context.h"
#include "testutil.h"

#include <memory>
#include <vector>

namespace {

constexpr uint32_t kCounter = 0x800749E0u;
constexpr uint32_t kRootSlot = 0x80073928u;
constexpr uint32_t kCallbackTable = 0x800749C0u;
constexpr uint32_t kIStat = 0x1F801070u;
constexpr uint32_t kIMask = 0x1F801074u;
constexpr uint32_t kRoot = 0x80011000u;
constexpr uint32_t kResume = 0x80011010u;
constexpr uint32_t kRootBody = 0x80011014u;
constexpr uint32_t kFirstCallback = 0x80011020u;
constexpr uint32_t kCallbackCount = 8u;
constexpr uint32_t kCallCounts = 0x80012000u;
constexpr uint32_t kRootCalls = kCallCounts + kCallbackCount * 4u;
constexpr uint32_t kResumeCalls = kRootCalls + 4u;
constexpr uint32_t kContextBuffer = 0x80012100u;
constexpr uint32_t kSavedSp = 0x80014000u;
constexpr uint32_t kSavedFp = 0x80014100u;
constexpr uint32_t kSavedGp = 0x80015000u;

void increment(Core &core, uint32_t address) {
  core.mem_w32(address, core.mem_r32(address) + 1u);
}

void clobberRegisters(Core &core) {
  for (uint32_t reg = 1; reg < 32; ++reg) {
    core.r[reg] = 0xA1000000u + reg;
  }
  core.hi = 0xA2000000u;
  core.lo = 0xA3000000u;
}

void callback(Core *core) {
  CHECK(core->pc >= kFirstCallback);
  const uint32_t slot = (core->pc - kFirstCallback) / 4u;
  CHECK(slot < kCallbackCount);
  increment(*core, kCallCounts + slot * 4u);
  clobberRegisters(*core);
}

// Synthetic callbacks, not a reimplementation of the retained PSYQ IRQ dispatcher. The same
// registered root is reached by the title scheduler and by the real HookEntryInt delivery path.
void root(Core *core) {
  increment(*core, kRootCalls);
  increment(*core, kCounter);
  for (uint32_t slot = 0; slot < kCallbackCount; ++slot) {
    psx::cpu::dispatchGuestToReturn0(*core,
                                     core->mem_r32(kCallbackTable + slot * 4u),
                                     psx::cpu::ExecutionBudget::currentTurn(*core),
                                     "synthetic-field-callback");
  }
}

void resume(Core *core) {
  increment(*core, kResumeCalls);
  CHECK_EQ(core->r[2], 1u);
  CHECK_EQ(core->r[29], kSavedSp);
  CHECK_EQ(core->r[30], kSavedFp);
  CHECK_EQ(core->r[28], kSavedGp);
  for (uint32_t reg = 0; reg < 8; ++reg) {
    CHECK_EQ(core->r[16 + reg], 0xB0000000u + reg);
  }
  CHECK_EQ(core->mem_r32(kIStat) & core->mem_r32(kIMask) & 1u, 1u);
  core->mem_w32(kIStat, 0x7FEu); // acknowledge only the VBlank source through guest MMIO
  psx::cpu::dispatchGuestToReturn0(*core,
                                   core->mem_r32(kRootSlot),
                                   psx::cpu::ExecutionBudget::currentTurn(*core),
                                   "synthetic-field-root");
  core->game->hle.dispatchBios('B', 0x17); // real non-returning ReturnFromException
  CHECK(false);
}

class FieldFixture {
public:
  FieldFixture() : fields(*game) {
    game->core.gameCtx = &context;
    game->runtime = &runtime;
    // Exercise the actual Core presentation backend without creating a graphics device/window.
    game->gpu_dev.s_gpu_on = 0;
  }

  bool install() {
    Core &core = game->core;
    const auto image = core.imageCatalog().activate(
        "synthetic-field", {kRoot & 0x1FFFFFFFu, (kFirstCallback & 0x1FFFFFFFu) + 32u}, 1u);
    if (!core.nativeDispatcher().install({{image, kRootBody}, "synthetic-field-root", root}) ||
        !core.nativeDispatcher().install({{image, kResume}, "synthetic-field-resume", resume})) {
      return false;
    }
    // The root enters through shipping JIT execution, which can service pending IRQ work before
    // reaching its native body. A native override at kRoot would bypass that important boundary.
    core.mem_w32(kRoot, 0x08000000u | ((kRootBody >> 2u) & 0x03FFFFFFu)); // j root body
    core.mem_w32(kRoot + 4u, 0u);                                         // nop delay slot
    for (uint32_t slot = 0; slot < kCallbackCount; ++slot) {
      const uint32_t address = kFirstCallback + slot * 4u;
      if (!core.nativeDispatcher().install(
              {{image, address}, "synthetic-field-callback", callback})) {
        return false;
      }
      core.mem_w32(kCallbackTable + slot * 4u, address);
    }
    core.mem_w32(kRootSlot, kRoot);
    core.mem_w32(kContextBuffer, kResume);
    core.mem_w32(kContextBuffer + 4u, kSavedSp);
    core.mem_w32(kContextBuffer + 8u, kSavedFp);
    for (uint32_t reg = 0; reg < 8; ++reg) {
      core.mem_w32(kContextBuffer + 0x0Cu + reg * 4u, 0xB0000000u + reg);
    }
    core.mem_w32(kContextBuffer + 0x2Cu, kSavedGp);
    core.r[4] = kContextBuffer;
    if (!game->hle.dispatchBios('B', 0x19)) {
      return false;
    }
    game->hle.irq_enabled = 1;
    core.mem_w32(kIMask, 1u);
    core.mem_w32(kIStat, 0u);
    core.pending_work = 0;
    for (uint32_t reg = 1; reg < 32; ++reg) {
      core.r[reg] = 0xC0000000u + reg;
    }
    core.hi = 0xC1000000u;
    core.lo = 0xC2000000u;
    core.pc = 0x80016000u;
    saved = static_cast<const R3000 &>(core);
    return true;
  }

  void checkRegisters() const {
    const Core &core = game->core;
    for (uint32_t reg = 0; reg < 32; ++reg) {
      CHECK_EQ(core.r[reg], saved.r[reg]);
    }
    CHECK_EQ(core.hi, saved.hi);
    CHECK_EQ(core.lo, saved.lo);
    CHECK_EQ(core.pc, saved.pc);
    CHECK_EQ(core.active_native_address, 0u);
    CHECK_EQ(game->hle.in_irq, 0);
    CHECK_EQ(game->hle.custom_exit_active, 0);
  }

  void checkCallbacks(uint32_t count) const {
    CHECK_EQ(game->core.mem_r32(kRootCalls), count);
    for (uint32_t slot = 0; slot < kCallbackCount; ++slot) {
      CHECK_EQ(game->core.mem_r32(kCallCounts + slot * 4u), count);
    }
  }

  spyro1::Spyro1Runtime runtime;
  SpyroContext context;
  std::unique_ptr<Game> game = std::make_unique<Game>();
  spyro1::FieldScheduler fields;
  R3000 saved{};
};

void test_unpresented_field_advances_time_and_full_root_once() {
  FieldFixture fixture;
  CHECK(fixture.install());
  CHECK(fixture.fields.deliver({"test-direct", false, false, false}));
  fixture.checkRegisters();
  fixture.checkCallbacks(1u);
  CHECK(fixture.game->core.lightrecExecutor().counters().executedBlocks > 0u);
  CHECK_EQ(fixture.fields.counter(), 1);
  CHECK_EQ(fixture.context.run.fields(), 1u);
  CHECK_EQ(fixture.fields.fieldsThisLogicFrame(), 1u);
  CHECK_EQ(fixture.game->presentation.fence(), 0u);
  const auto fieldTicks =
      display_field_cpu_ticks(1, 1, gpu_field_rate_millihz(&fixture.game->core));
  CHECK_EQ(fixture.game->timing.guestInstructionTicks, 2u);
  CHECK_EQ(fixture.game->timing.emulatedCpuTicks(), fieldTicks + 2u);
}

void test_pending_edge_uses_hook_continuation_once() {
  FieldFixture fixture;
  CHECK(fixture.install());
  gpu_pace_subframe_fields(&fixture.game->core, 1, 1);
  CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 1u);
  CHECK(fixture.fields.deliver({"test-pending-edge", false, false, false}));
  psx::cpu::servicePendingWork(fixture.game->core);
  fixture.checkRegisters();
  fixture.checkCallbacks(1u);
  CHECK_EQ(fixture.game->core.mem_r32(kResumeCalls), 1u);
  CHECK_EQ(fixture.fields.counter(), 1);
  CHECK_EQ(fixture.context.run.fields(), 1u);
  CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 0u);
  CHECK_EQ(fixture.game->core.pending_work & Core::PW_IRQ, 0u);
}

void test_presentation_after_delivered_field_does_not_deliver_another_root() {
  FieldFixture fixture;
  CHECK(fixture.install());
  CHECK(fixture.fields.deliver({"test-before-present", false, false, false}));
  fixture.checkRegisters();
  fixture.checkCallbacks(1u);

  const auto deliveredTime = fixture.game->timing.emulatedCpuTicks();
  // The actual Core backend delegates to Spyro 1's runtime pacing owner. The former combined
  // pacer call here reproduced counter=2 while the scheduler and RuntimeRun each counted one.
  fixture.game->presentation.commit(&fixture.game->core, 1);
  psx::cpu::servicePendingWork(fixture.game->core);
  fixture.checkRegisters();
  CHECK_EQ(fixture.context.run.fields(), 1u);
  CHECK_EQ(fixture.fields.fieldsThisLogicFrame(), 1u);
  CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 0u);
  CHECK_EQ(fixture.game->core.pending_work & Core::PW_IRQ, 0u);
  CHECK_EQ(fixture.fields.counter(), 1);
  fixture.checkCallbacks(1u);
  CHECK_EQ(fixture.game->presentation.fence(), 1u);
  CHECK_EQ(fixture.game->timing.emulatedCpuTicks(), deliveredTime);
}

void test_root_without_hook_survives_pending_work_inside_guest_dispatch() {
  FieldFixture fixture;
  CHECK(fixture.install());
  CHECK(fixture.game->hle.dispatchBios('B', 0x18));
  static_cast<R3000 &>(fixture.game->core) = fixture.saved;
  gpu_pace_subframe_fields(&fixture.game->core, 1, 1);
  CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 1u);
  CHECK((fixture.game->core.pending_work & Core::PW_IRQ) != 0u);
  CHECK(fixture.fields.deliver({"test-no-hook", false, false, false}));
  psx::cpu::servicePendingWork(fixture.game->core);
  fixture.checkRegisters();
  fixture.checkCallbacks(1u);
  CHECK(fixture.game->core.lightrecExecutor().counters().executedBlocks > 0u);
  CHECK_EQ(fixture.fields.counter(), 1);
  CHECK_EQ(fixture.context.run.fields(), 1u);
  CHECK_EQ(fixture.game->core.mem_r32(kResumeCalls), 0u);
  // No registered IRQ path owns the hardware acknowledgement. The scheduler's direct root still
  // runs once; Hle retires the service gate without inventing a guest write to the VBlank latch.
  CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 1u);
  CHECK_EQ(fixture.game->core.pending_work & Core::PW_IRQ, 0u);
}

class EmptyTemporalScene final : public TemporalSceneSource {
public:
  bool eligible(const Core &) const override {
    return true;
  }
  bool owns(const RqItem &) const override {
    return false;
  }
  void reconstruct(Core &, float t) override {
    reconstructed.push_back(t);
  }
  void rotate(Core &) override {}

  std::vector<float> reconstructed;
};

void test_temporal_split_presents_do_not_advance_delivered_time() {
  FieldFixture fixture;
  CHECK(fixture.install());
  auto source = std::make_unique<EmptyTemporalScene>();
  auto *observedSource = source.get();
  auto temporal = std::make_unique<Fps60>(*fixture.game, std::move(source));
  auto *presentation = temporal.get();
  fixture.game->temporalPresentation = std::move(temporal);
  fixture.game->mods.fps60 = true;
  for (uint32_t frame = 0; frame < 2; ++frame) {
    fixture.fields.beginLogicFrame();
    CHECK(fixture.fields.deliver({"test-temporal-first", false, false, false}));
    CHECK(fixture.fields.deliver({"test-temporal-second", false, false, false}));
    CHECK(fixture.fields.finishLogicFrame());
    const auto deliveredTime = fixture.game->timing.emulatedCpuTicks();
    // First commit seeds temporal history; the second executes Fps60's actual two-slot path,
    // including both calls through CoreFramePresentationBackend::pace to the runtime owner.
    presentation->frame_commit(&fixture.game->core, 2);
    psx::cpu::servicePendingWork(fixture.game->core);
    fixture.checkRegisters();
    fixture.checkCallbacks((frame + 1u) * 2u);
    CHECK_EQ(fixture.fields.counter(), (frame + 1u) * 2u);
    CHECK_EQ(fixture.context.run.fields(), (frame + 1u) * 2u);
    CHECK_EQ(fixture.game->timing.emulatedCpuTicks(), deliveredTime);
    CHECK_EQ(fixture.game->presentation.fence(), frame + 1u);
    CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 0u);
    CHECK_EQ(fixture.game->core.pending_work & Core::PW_IRQ, 0u);
  }
  CHECK(observedSource->reconstructed == std::vector<float>({1.0f, 0.5f, 1.0f}));
}

void test_default_runtime_presentation_still_delivers_hardware_fields() {
  FieldFixture fixture;
  CHECK(fixture.install());
  // An ordinary runtime's existing combined contract remains available. Qualify the base method
  // explicitly rather than borrowing another title or changing Spyro 1's runtime ownership.
  fixture.runtime.GameRuntime::pacePresentation(fixture.game->core, 1, 2);
  psx::cpu::servicePendingWork(fixture.game->core);
  CHECK_EQ(fixture.fields.counter(), 0);
  fixture.runtime.GameRuntime::pacePresentation(fixture.game->core, 1, 2);
  psx::cpu::servicePendingWork(fixture.game->core);
  fixture.checkRegisters();
  fixture.checkCallbacks(1u);
  CHECK_EQ(fixture.fields.counter(), 1);
  CHECK_EQ(fixture.context.run.fields(), 0u);
  CHECK_EQ(fixture.game->core.mem_r32(kResumeCalls), 1u);
}

void test_masked_hook_edge_dispatches_once_when_unmasked() {
  FieldFixture fixture;
  CHECK(fixture.install());
  fixture.game->core.mem_w32(kIMask, 0u);
  CHECK(fixture.fields.deliver({"test-masked-hook", false, false, false}));
  CHECK(fixture.fields.deliver({"test-second-masked-hook", false, false, false}));
  const int maskedCounter = fixture.fields.counter();
  fixture.checkRegisters();
  CHECK_EQ(fixture.context.run.fields(), 2u);
  CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 1u);
  fixture.game->core.mem_w32(kIMask, 1u);
  psx::cpu::servicePendingWork(fixture.game->core);
  fixture.checkRegisters();
  CHECK_EQ(fixture.fields.counter(), 1);
  CHECK_EQ(maskedCounter, 0);
  // Two physical fields while masked coalesce into one pending hardware edge, not two callbacks.
  fixture.checkCallbacks(1u);
  CHECK_EQ(fixture.game->core.mem_r32(kResumeCalls), 1u);
  CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 0u);
}

void test_critical_section_defers_hook_root_until_irq_service_resumes() {
  FieldFixture fixture;
  CHECK(fixture.install());
  fixture.game->hle.irq_enabled = 0;
  CHECK(fixture.fields.deliver({"test-critical-hook", false, false, false}));
  fixture.checkRegisters();
  fixture.checkCallbacks(0u);
  CHECK_EQ(fixture.fields.counter(), 0);
  CHECK_EQ(fixture.context.run.fields(), 1u);
  CHECK_EQ(fixture.fields.fieldsThisLogicFrame(), 1u);
  CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 1u);
  CHECK((fixture.game->core.pending_work & Core::PW_IRQ) != 0u);
  fixture.game->hle.irq_enabled = 1;
  psx::cpu::servicePendingWork(fixture.game->core);
  fixture.checkRegisters();
  fixture.checkCallbacks(1u);
  CHECK_EQ(fixture.fields.counter(), 1);
  CHECK_EQ(fixture.context.run.fields(), 1u);
  CHECK_EQ(fixture.game->core.mem_r32(kResumeCalls), 1u);
  CHECK_EQ(fixture.game->core.mem_r32(kIStat) & 1u, 0u);
  CHECK_EQ(fixture.game->core.pending_work & Core::PW_IRQ, 0u);
}

void test_bootstrap_without_root_keeps_host_counter_ownership() {
  FieldFixture fixture;
  CHECK(fixture.install());
  CHECK(fixture.game->hle.dispatchBios('B', 0x18));
  static_cast<R3000 &>(fixture.game->core) = fixture.saved;
  fixture.game->core.mem_w32(kRootSlot, 0u);
  CHECK(fixture.fields.deliver({"test-bootstrap", false, false, false}));
  psx::cpu::servicePendingWork(fixture.game->core);
  fixture.checkRegisters();
  fixture.checkCallbacks(0u);
  CHECK_EQ(fixture.fields.counter(), 1);
  CHECK_EQ(fixture.context.run.fields(), 1u);
  const auto fieldTicks =
      display_field_cpu_ticks(1, 1, gpu_field_rate_millihz(&fixture.game->core));
  CHECK_EQ(fixture.game->timing.emulatedCpuTicks(), fieldTicks);
}

} // namespace

int main() {
  // The production NOPACE contract removes host sleeps while retaining simulated display edges.
  psx::config::cv_nopace.set(psx::config::Layer::Runtime, true);
  psx::config::cv_repl.set(psx::config::Layer::Runtime, false);
  RUN(unpresented_field_advances_time_and_full_root_once);
  RUN(pending_edge_uses_hook_continuation_once);
  RUN(root_without_hook_survives_pending_work_inside_guest_dispatch);
  RUN(presentation_after_delivered_field_does_not_deliver_another_root);
  RUN(temporal_split_presents_do_not_advance_delivered_time);
  RUN(default_runtime_presentation_still_delivers_hardware_fields);
  RUN(masked_hook_edge_dispatches_once_when_unmasked);
  RUN(critical_section_defers_hook_root_until_irq_service_resumes);
  RUN(bootstrap_without_root_keeps_host_counter_ownership);
  return pt_summary();
}
