#include "field_moby_lists.h"

#include "core.h"
#include "guest_call.h"

namespace {

constexpr uint32_t kBuildMobyLists = 0x800521c0u;

} // namespace

void spyro::field_moby_lists::build(Core *core) {
  psx::cpu::dispatchGuestToReturn0(
      *core, kBuildMobyLists, psx::cpu::ExecutionBudget::currentTurn(*core), "build-moby-lists");
}
