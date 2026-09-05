#include "core.h"
#include "execution_control.h"
#include "game.h"
#include "guest_execution.h"
#include "image_identity.h"
#include "lightrec_executor.h"
#include "native_dispatch.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <lucent/log.h>
#include <memory>

namespace {
constexpr std::uint32_t kEntry = 0x80010000u;
constexpr std::uint32_t kReturn = 0x80020000u;

void require(bool condition, const char *message) {
  if (!condition) {
    lucent::error("test", "{}", message);
    std::exit(1);
  }
}

void load(Core &core) {
  // The nested loop changes ra and crosses many execution budgets before returning to its caller.
  constexpr std::array code{
      0x03e08021u, // addu s0,ra,zero
      0x0c004008u, // jal 0x80010020
      0x00000000u, // nop
      0x24420007u, // addiu v0,v0,7 (must still execute after the nested call)
      0x02000008u, // jr s0
      0x00000000u, // nop
      0x00000000u,
      0x00000000u,
      0x240803e8u, // addiu t0,zero,1000
      0x24420001u, // addiu v0,v0,1
      0x2508ffffu, // addiu t0,t0,-1
      0x1500fffdu, // bne t0,zero,0x80010024
      0x00000000u, // nop
      0x03e00008u, // jr ra
      0x00000000u, // nop
  };
  for (std::size_t index = 0; index < code.size(); ++index) {
    core.mem_w32(kEntry + static_cast<std::uint32_t>(index * 4), code[index]);
  }
  core.imageCatalog().activate("synthetic nested call", {0x10000u, 0x10100u}, 1);
  core.r[31] = kReturn;
}

void checkNestedReturn(std::uint64_t budget, bool expectYields) {
  auto game = std::make_unique<Game>();
  Core *core = &game->core;
  load(*core);
  spyro::GuestExecution execution(*core, kEntry);
  psx::cpu::ExecutionResult result;
  unsigned yields = 0;
  do {
    result = execution.step(psx::cpu::ExecutionBudget::fromCycles(budget));
    if (result.reason == psx::cpu::ExecutionExitReason::BudgetExhausted) {
      ++yields;
      require(core->pc == result.guestPc, "yield must report the committed continuation");
      require(yields < 1000, "nested call did not complete within the test bound");
    }
  } while (result.reason == psx::cpu::ExecutionExitReason::BudgetExhausted);
  require(result.returned() && result.guestPc == kReturn,
          "must return to the original root caller");
  require(core->r[2] == 1007, "must execute the nested body once and the outer continuation");
  require((yields != 0) == expectYields, "test must distinguish sliced and unsliced execution");
  const auto counters = core->lightrecExecutor().counters();
  require(counters.executedBlocks > 0 && counters.fallback.calls == 0,
          "test requires JIT execution");
  require(execution.step(psx::cpu::ExecutionBudget::fromCycles(budget)).returned(),
          "return must remain terminal");
  require(core->lightrecExecutor().counters().calls == counters.calls,
          "terminal step must execute nothing");
  lucent::info("test",
               "nested return: budget={} yields={} blocks={} fallback={}",
               budget,
               yields,
               counters.executedBlocks,
               counters.fallback.calls);
}

void yieldFrame(Core *core) {
  psx::cpu::requestExecutionExit(*core, psx::cpu::ExecutionExitReason::FrameBoundary);
}

void checkStoppedBoundary(bool nativeFrame) {
  auto game = std::make_unique<Game>();
  Core *core = &game->core;
  load(*core);
  if (nativeFrame) {
    const auto identity = core->currentImageIdentity(kEntry);
    require(identity.has_value(), "fixture image must resolve");
    require(core->nativeDispatcher().install({{*identity, kEntry}, "frame", yieldFrame}),
            "install frame owner");
  }
  spyro::GuestExecution execution(*core, nativeFrame ? kEntry : kEntry + 0x1000u);
  const auto result = execution.step(psx::cpu::ExecutionBudget::fromCycles(100));
  const auto expected = nativeFrame ? psx::cpu::ExecutionExitReason::FrameBoundary
                                    : psx::cpu::ExecutionExitReason::Fault;
  require(result.reason == expected, "unhandled frame or fault must propagate");
  const auto calls = core->lightrecExecutor().counters().calls;
  require(execution.step(psx::cpu::ExecutionBudget::fromCycles(100)).reason == expected,
          "stop must remain visible");
  require(core->lightrecExecutor().counters().calls == calls,
          "must not silently resume an unhandled exit");
}
} // namespace

int main() {
  checkNestedReturn(100'000, false);
  checkNestedReturn(64, true);
  checkStoppedBoundary(false);
  checkStoppedBoundary(true);
  lucent::info("test",
               "guest execution: 4/4 scenarios passed (unsliced, sliced, fault, native frame)");
}
