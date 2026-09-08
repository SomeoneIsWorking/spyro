#include "archive_transfer.h"
#include "content_identity.h"
#include "core.h"
#include "execution_control.h"
#include "game.h"
#include "image_identity.h"
#include "lightrec_executor.h"
#include "native_dispatch.h"
#include "spyro_context.h"
#include "spyro_game.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <lucent/log.h>
#include <memory>
#include <span>
#include <vector>

namespace {

constexpr std::uint32_t kDestination = 0x80090000u;
constexpr spyro::ArchiveRead kRead{37u, kDestination, 4096u, 2048u, 5u};

void require(bool condition, const char *message) {
  if (!condition) {
    lucent::error("archive-test", "{}", message);
    std::exit(1);
  }
}

void executeImage(Core &core, std::uint32_t expected) {
  core.r[31] = 0x800F0000u;
  const auto result =
      psx::cpu::dispatchGuest(core, kDestination, psx::cpu::ExecutionBudget::fromCycles(4096u));
  require(result.returned(), "loaded synthetic image must return through the shipping JIT");
  require(core.r[2] == expected, "replacement instructions were not executed");
  const auto counters = core.lightrecExecutor().counters();
  require(counters.executedBlocks != 0u && counters.fallback.calls == 0u,
          "image replacement must execute dynamically without fallback");
}

void checkTransfers() {
  auto first = std::make_unique<Game>();
  auto second = std::make_unique<Game>();
  SpyroContext firstContext;
  SpyroContext secondContext;
  first->core.gameCtx = &firstContext;
  second->core.gameCtx = &secondContext;
  auto &owner = spyro_context(first->core).archiveTransfer;
  std::uint32_t value = 7u;
  std::vector<std::uint32_t> sectors;
  const spyro::ArchiveTransfer::SectorReader reader = [&](std::uint32_t lba, auto bytes) {
    sectors.push_back(lba);
    std::ranges::fill(bytes, 0u);
    // Synthetic addiu v0,zero,value; jr ra; nop. No retained/generated guest body is used.
    const std::array<std::uint32_t, 3> instructions{0x24020000u | value, 0x03E00008u, 0u};
    for (std::size_t word = 0; word < instructions.size(); ++word) {
      for (unsigned byte = 0; byte < 4; ++byte) {
        bytes[word * 4 + byte] = static_cast<std::uint8_t>(instructions[word] >> (byte * 8u));
      }
    }
    return true;
  };

  auto decision = owner.read(first->core, kRead, true, reader);
  require(decision.accepted(), "complete request was refused");
  require(sectors == std::vector<std::uint32_t>{38u, 39u},
          "reader must include both the archive base and byte offset");
  const auto oldIdentity = first->core.currentImageIdentity(kDestination);
  require(oldIdentity.has_value(), "complete request must activate a resident image");
  executeImage(first->core, value);
  require(!spyro_context(second->core).archiveTransfer.takeCompletion(),
          "another Core consumed the pending completion");
  require(owner.takeCompletion() && !owner.takeCompletion(),
          "completion must be delivered exactly once to its owning Core");

  const std::span oldBytes{first->core.ram + (kDestination & 0x1FFFFFFFu), kRead.length};
  const std::vector<std::uint8_t> before(oldBytes.begin(), oldBytes.end());
  std::uint32_t reads = 0;
  const auto truncated = owner.read(first->core, kRead, true, [&](std::uint32_t, auto bytes) {
    std::ranges::fill(bytes, 0xCCu);
    return reads++ == 0u;
  });
  require(reads == 2u && !truncated.accepted(), "truncated source must be exercised and refused");
  require(truncated.transfer.movedBytes == 0u && !owner.takeCompletion(),
          "truncated source must publish neither RAM coverage nor completion");
  require(std::ranges::equal(oldBytes, before), "short read changed the previous complete image");
  require(first->core.currentImageIdentity(kDestination) == oldIdentity,
          "short read published a replacement image identity");
  const auto fault = first->core.executionControl().consume();
  require(fault && fault->reason == psx::cpu::ExecutionExitReason::Fault,
          "short read must request an explicit runtime fault");
  executeImage(first->core, value);

  value = 11u;
  sectors.clear();
  decision = owner.read(first->core, kRead, false, reader);
  require(decision.accepted() && !owner.takeCompletion(),
          "synchronous request must complete without a deferred callback");
  require(first->core.currentImageIdentity(kDestination) != oldIdentity,
          "address reuse retained the previous image identity");
  executeImage(first->core, value);

  auto invalid = kRead;
  invalid.destination = 0x801FFFFFu;
  reads = 0u;
  const auto rejected = owner.read(first->core, invalid, true, [&](std::uint32_t, auto) {
    ++reads;
    return true;
  });
  require(!rejected.accepted() && reads == 0u,
          "out-of-RAM request must fail before touching the disc or RAM");
}

void checkLoaderFault(std::uint32_t entry) {
  auto game = std::make_unique<Game>();
  SpyroContext context;
  game->core.gameCtx = &context;
  auto &core = game->core;
  // Refuse at the production disc-owner boundary without consulting the operator's real media.
  core.game = nullptr;
  core.imageCatalog().activate("synthetic loader registration", {0x10000u, 0x70000u}, 1u);
  spyro_register_cd_queue(core);
  core.r[4] = kRead.baseLba;
  core.r[5] = kRead.destination;
  core.r[6] = kRead.length;
  core.r[7] = kRead.byteOffset;
  core.r[29] = 0x801FF000u;
  core.r[31] = 0x800F0000u;
  core.mem_w32(core.r[29] + 16u, kRead.token);
  core.mem_w32(0x80076BB8u, 23u);
  const auto result =
      psx::cpu::dispatchGuest(core, entry, psx::cpu::ExecutionBudget::fromCycles(4096u));
  require(result.reason == psx::cpu::ExecutionExitReason::Fault,
          "native dispatch swallowed the missing-disc fault and returned to a void caller");
  require(core.mem_r32(0x80076BB8u) == 23u,
          "failed native loader falsely published a completed guest gate");
  require(!context.archiveTransfer.takeCompletion(), "failed native loader armed completion");
}

} // namespace

int main() {
  constexpr std::array<std::uint8_t, 3> abc{'a', 'b', 'c'};
  require(spyro::sha256(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "content identity must match the SHA-256 known-answer vector");
  checkTransfers();
  checkLoaderFault(0x80016500u);
  checkLoaderFault(0x80016698u);
  lucent::info("archive-test",
               "PASS: complete/short reads, per-Core completion, JIT image replacement, "
               "bounds refusal, and both native loader faults");
}
