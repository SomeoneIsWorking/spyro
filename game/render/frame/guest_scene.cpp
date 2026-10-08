#include "guest_scene.h"

#include "core.h"
#include "native_dispatch.h"
#include <lucent/log.h>

spyro::render::GuestSceneStep spyro::render::drawSceneWithGuestArm(Core &core, std::uint32_t arm) {
  GuestSceneStep step{.arm = arm};
  if (arm == 0u) {
    // No guest body to run for a stage whose arm is data rather than code.
    step.stop = GuestSceneStop::Refused;
    step.reason = "the scene classifier recovered no guest render-arm address for this stage";
    return step;
  }
  if (!core.currentImageIdentity(arm).has_value()) {
    step.stop = GuestSceneStop::Refused;
    step.reason = "the guest render arm is in no loaded code image";
    return step;
  }
  // The arm's scene producers all run, then its display tail's first field wait raises the
  // framework's FrameBoundary exit; the tail that follows is frameEnd's.
  const psx::cpu::ExecutionResult result =
      psx::cpu::dispatchGuestUntilExit(core, arm, psx::cpu::ExecutionBudget::currentTurn(core));
  step.guestPc = result.guestPc;
  step.cycles = result.cycles;
  switch (result.reason) {
  case psx::cpu::ExecutionExitReason::FrameBoundary:
    // The guest asked for a display field. Everything it drew is submitted; the port spends the
    // field and presents.
    step.stop = GuestSceneStop::AskedForField;
    step.reason = result.detail;
    break;
  case psx::cpu::ExecutionExitReason::GuestReturn:
    step.stop = GuestSceneStop::Drawn;
    step.reason = result.detail;
    break;
  default:
    // An arm that consumed a whole display field has submitted a scene the host is about to finish
    // for it, so it gets the same answer as the field wait. A slower picture, never a dead product.
    step.stop = result.reason == psx::cpu::ExecutionExitReason::BudgetExhausted
                    ? GuestSceneStop::AskedForField
                    : GuestSceneStop::Refused;
    step.reason = result.detail;
    break;
  }
  return step;
}