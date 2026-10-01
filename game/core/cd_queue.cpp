#include "archive_transfer.h"
#include "core.h"
#include "execution_control.h"
#include "field_owner.h"
#include "guest_call.h"
#include "load_ledger.h"
#include "native_execution.h"
#include "spyro_context.h"
#include "spyro_game.h"

#include <cstdint>
#include <lucent/log.h>
#include <string>
#include <string_view>

namespace {

constexpr uint32_t kGate = 0x80076BB8u;
constexpr uint32_t kRequestToken = 0x800756E0u;
constexpr uint32_t kCallbackState = 0x8007588Cu;
constexpr uint32_t kTransferSectorCount = 0x80076B94u;
constexpr uint32_t kTransferPosition = 0x80076B98u;
constexpr uint32_t kTransferDestination = 0x80076B9Cu;
// g_LoadStage and g_CdMusic.m_Flags, the two guest words 0155 §2.2 says every cooperative loader
// reads before it advances a stage. Read once per operation and never written: the ledger is a
// record of what the loaders saw, and a diagnostic that moved either word would be changing the
// thing it measures.
constexpr uint32_t kLoadStage = 0x80075864u;
constexpr uint32_t kCdMusicFlags = 0x800774B4u;
constexpr uint32_t kCdMusicIdle = 0x40u;

void cd_retry_step(Core *core) {
  psx::cpu::callOriginalToReturn(
      *core, 0x800163E4u, psx::cpu::ExecutionBudget::currentTurn(*core), "cd-retry-step");
  if (spyro_context(*core).archiveTransfer.takeCompletion()) {
    psx::cpu::dispatchGuestToReturn1(
        *core, 0x80016490u, 2u, psx::cpu::ExecutionBudget::currentTurn(*core), "cd-complete");
    auto &ledger = spyro_context(*core).loadLedger;
    const auto *owner = spyro::fieldOwnerIfPublished(*core);
    ledger.completePending(owner != nullptr ? owner->fields() : 0u,
                           static_cast<int32_t>(core->mem_r32(kLoadStage)));
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
  auto &ledger = spyro_context(core).loadLedger;
  // `$ra` inside the override is the address after the guest's `jal`, so the issuer site is eight
  // bytes back, and those are the 31 addresses the census enumerates -- which is what makes the
  // ledger's coverage denominator mean anything. It resolves the site ONLY when the caller was
  // guest code: `BootSequence::loadAssets` dispatches the loader itself, and one guest PETE load
  // arrives with the return address of the call to the OUTER loader function. The guest PC is
  // recorded beside the site so those operations are visibly unattributed rather than carrying a
  // plausible-looking guest address (instrument I058, and 0155's "$ra - 8 failure mode").
  const uint32_t issuerSite = core.r[31] - 8u;
  const auto *owner = spyro::fieldOwnerIfPublished(core);
  const uint64_t field = owner != nullptr ? owner->fields() : 0u;
  const auto index =
      ledger.begin({.issuerSite = issuerSite,
                    .guestPc = core.pc,
                    .baseLba = read.baseLba,
                    .byteOffset = read.byteOffset,
                    .length = read.length,
                    .destination = read.destination,
                    .deferred = deferred,
                    .accepted = true,
                    .musicGateClearAtIssue = (core.mem_r32(kCdMusicFlags) & kCdMusicIdle) == 0u,
                    .fieldCounted = owner != nullptr,
                    .fieldAtIssue = field,
                    .loadStageAtIssue = static_cast<int32_t>(core.mem_r32(kLoadStage))});
  const auto decision = spyro_context(core).archiveTransfer.read(
      core, read, deferred, [&ledger, index](std::string_view digest) {
        ledger.attachDigest(index, std::string(digest));
      });
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
    ledger.markRefused(index);
    return;
  }
  publishTransferState(core, read, deferred);
  if (deferred) {
    core.r[2] = decision.returnValue();
    return;
  }
  // A blocking issuer's payload is in guest RAM before the override returns, so its completion is
  // the field it was issued in. The streaming form leaves the gate at 1 and completes when the
  // guest calls the retry step, which is the only place a latency can accumulate.
  ledger.completeAtIssue(index);
  core.r[2] = 2u;
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
