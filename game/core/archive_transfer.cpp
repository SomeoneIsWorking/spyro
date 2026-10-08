#include "archive_transfer.h"

#include "core.h"
#include "disc.h"
#include "execution_control.h"
#include "game.h"
#include "image_publication.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace spyro {
namespace {

archive_transfer::Decision refuse(Core &core, const ArchiveRead &request, std::string detail) {
  psx::cpu::requestExecutionExit(core,
                                 {.reason = psx::cpu::ExecutionExitReason::Fault,
                                  .guestPc = core.pc,
                                  .detail = "Spyro WAD read refused: " + std::move(detail)});
  return {.transfer = {.requestedBytes = request.length, .movedBytes = 0u}, .refused = true};
}

} // namespace

archive_transfer::Decision
ArchiveTransfer::read(Core &core, const ArchiveRead &request, bool deferred) {
  return read(core, request, deferred, [&core](std::uint32_t lba, auto sector) {
    return core.game != nullptr && disc_read_sector(&core.game->disc, lba, sector.data()) != 0;
  });
}

archive_transfer::Decision ArchiveTransfer::read(Core &core,
                                                 const ArchiveRead &request,
                                                 bool deferred,
                                                 const SectorReader &reader) {
  completionPending_ = false;
  const auto destination = request.destination & 0x1fffffffu;
  if (request.destination == 0u || destination >= sizeof core.ram ||
      request.length > sizeof core.ram - destination) {
    return refuse(core, request, "destination does not fit guest main RAM");
  }
  // Loaders address whole sectors; an unaligned request is unsupported, not rounded down.
  if (request.byteOffset % 2048u != 0u) {
    return refuse(core, request, "archive offset is not sector aligned");
  }
  const auto start = static_cast<std::uint64_t>(request.baseLba) + request.byteOffset / 2048u;
  const auto sectorCount = (request.length + 2047u) / 2048u;
  if (start + sectorCount > std::numeric_limits<std::uint32_t>::max()) {
    return refuse(core, request, "sector range overflows");
  }

  // Stage the whole request first: a short source must not half-replace a resident image.
  std::vector<std::uint8_t> bytes(request.length);
  std::array<std::uint8_t, 2048> sector{};
  for (std::uint32_t offset = 0; offset < request.length;) {
    const auto lba = static_cast<std::uint32_t>(start + offset / sector.size());
    if (!reader(lba, sector)) {
      return refuse(core,
                    request,
                    "sector " + std::to_string(lba) + " unavailable after reading " +
                        std::to_string(offset) + " of " + std::to_string(request.length) +
                        " bytes; guest RAM unchanged");
    }
    const auto count = std::min<std::uint32_t>(request.length - offset, sector.size());
    std::copy_n(sector.begin(), count, bytes.begin() + offset);
    offset += count;
  }
  if (!bytes.empty()) {
    const auto content = image_publication::digest(bytes);
    if (!content) {
      return refuse(core, request, "SHA-256 calculation failed");
    }
    for (std::uint32_t offset = 0; offset < request.length; ++offset) {
      // mem_w8 owns executable invalidation and diagnostic write guards.
      core.mem_w8(request.destination + offset, bytes[offset]);
    }
    image_publication::activate(core, "WAD", {destination, destination + request.length}, *content);
  }
  const auto decision = archive_transfer::decide(request.length, request.length);
  completionPending_ = deferred && decision.completionPending();
  return decision;
}

bool ArchiveTransfer::takeCompletion() {
  return std::exchange(completionPending_, false);
}

} // namespace spyro
