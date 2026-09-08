#include "archive_transfer.h"
#include "core.h"
#include "execution_control.h"
#include "guest_call.h"
#include "native_execution.h"
#include "spyro_context.h"
#include "spyro_game.h"

#include <cstdint>
#include <lucent/log.h>

namespace {

constexpr uint32_t kGate = 0x80076BB8u;
constexpr uint32_t kRequestToken = 0x800756E0u;
constexpr uint32_t kCallbackState = 0x8007588Cu;
constexpr uint32_t kTransferSectorCount = 0x80076B94u;
constexpr uint32_t kTransferPosition = 0x80076B98u;
constexpr uint32_t kTransferDestination = 0x80076B9Cu;

void cd_retry_step(Core *core) {
  psx::cpu::callOriginalToReturn(
      *core, 0x800163E4u, psx::cpu::ExecutionBudget::currentTurn(*core), "cd-retry-step");
  if (spyro_context(*core).archiveTransfer.takeCompletion()) {
    psx::cpu::dispatchGuestToReturn1(
        *core, 0x80016490u, 2u, psx::cpu::ExecutionBudget::currentTurn(*core), "cd-complete");
    lucent::debug("cdq", "delivered CD completion -> gate now {}", core->mem_r32(kGate));
  }
}

spyro::ArchiveRead archiveReadFromRegisters(Core &core) {
  return {.baseLba = core.r[4],
          .destination = core.r[5],
          .length = core.r[6],
          .byteOffset = core.r[7],
          .token = core.mem_r32(core.r[29] + 16u)};
}

void publishTransferState(Core &core, const spyro::ArchiveRead &read, bool pending) {
  // The same retained Sony leaf used by both retail loader bodies owns LBA -> CdlLOC encoding.
  psx::cpu::dispatchGuestToReturn2(core,
                                   0x80064094u,
                                   read.baseLba + read.byteOffset / 2048u,
                                   kTransferPosition,
                                   psx::cpu::ExecutionBudget::currentTurn(core),
                                   "cd-position");
  core.mem_w32(kTransferSectorCount, (read.length + 2047u) / 2048u);
  core.mem_w32(kTransferDestination, read.destination);
  core.mem_w32(kGate, pending ? 1u : 0u);
  core.mem_w32(kCallbackState, 0u);
  core.mem_w32(kRequestToken, pending ? read.token : 0u);
}

void transfer(Core &core, bool deferred) {
  const auto read = archiveReadFromRegisters(core);
  const auto decision = spyro_context(core).archiveTransfer.read(core, read, deferred);
  lucent::debug("cdq",
                "{}: base={} dest=0x{:08X} len={} offset=0x{:08X} token=0x{:08X} -> "
                "coverage=[0,{}) complete={} accepted={}",
                deferred ? "stream" : "loader",
                read.baseLba,
                read.destination,
                read.length,
                read.byteOffset,
                read.token,
                decision.transfer.coveredEnd(),
                decision.transfer.complete() ? 1 : 0,
                decision.accepted() ? 1 : 0);
  if (!decision.accepted()) {
    // Both retail loaders are void: changing v0 cannot make their callers handle missing bytes.
    // The transfer owner requested a typed fault before any guest RAM/image publication.
    return;
  }
  publishTransferState(core, read, deferred);
  core.r[2] = deferred ? decision.returnValue() : 2u;
}

void cd_loader(Core *core) {
  transfer(*core, false);
}

void cd_stream_read(Core *core) {
  transfer(*core, true);
}

} // namespace

void spyro_register_cd_queue(Core &core) {
  spyro::installNativeOverride(core, 0x80016500u, "cd_loader", cd_loader);
  spyro::installNativeOverride(core, 0x80016698u, "cd_stream_read", cd_stream_read);
  spyro::installNativeOverride(core, 0x800163E4u, "cd_retry_step", cd_retry_step);
}
