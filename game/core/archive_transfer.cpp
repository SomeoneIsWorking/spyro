#include "archive_transfer.h"

#include "content_identity.h"
#include "core.h"
#include "disc.h"
#include "execution_control.h"
#include "game.h"
#include "image_identity.h"

#include <algorithm>
#include <array>
#include <charconv>
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
  // The retained loaders address sectors, and every measured offset is sector aligned (C020).
  // An unaligned request is a new unsupported contract, not permission to silently round it down.
  if (request.byteOffset % 2048u != 0u) {
    return refuse(core, request, "archive offset is not sector aligned");
  }
  const auto start = static_cast<std::uint64_t>(request.baseLba) + request.byteOffset / 2048u;
  const auto sectorCount = (request.length + 2047u) / 2048u;
  if (start + sectorCount > std::numeric_limits<std::uint32_t>::max()) {
    return refuse(core, request, "sector range overflows");
  }

  // Stage the bounded request before mutating RAM. A short source cannot corrupt a still-resident
  // image or leave its old identity attached to partially replaced instructions.
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
    const auto digest = sha256(bytes);
    if (digest.empty()) {
      return refuse(core, request, "SHA-256 calculation failed");
    }
    std::uint64_t digestPrefix = 0;
    const auto parsed = std::from_chars(digest.data(), digest.data() + 16, digestPrefix, 16);
    if (parsed.ec != std::errc{}) {
      return refuse(core, request, "invalid SHA-256 digest");
    }
    for (std::uint32_t offset = 0; offset < request.length; ++offset) {
      // The canonical memory writer owns executable invalidation and diagnostic write guards.
      core.mem_w8(request.destination + offset, bytes[offset]);
    }
    core.imageCatalog().activate(
        "WAD SHA-256 " + digest, {destination, destination + request.length}, digestPrefix);
  }
  const auto decision = archive_transfer::decide(request.length, request.length);
  completionPending_ = deferred && decision.completionPending();
  return decision;
}

bool ArchiveTransfer::takeCompletion() {
  return std::exchange(completionPending_, false);
}

} // namespace spyro
