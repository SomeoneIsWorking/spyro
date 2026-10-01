#pragma once

#include "archive_transfer_contract.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

class Core;

namespace spyro {

struct ArchiveRead {
  std::uint32_t baseLba;
  std::uint32_t destination;
  std::uint32_t length;
  std::uint32_t byteOffset;
  std::uint32_t token;
};

// One per Core: the read completion belongs to the instance that published the request.
class ArchiveTransfer {
public:
  using SectorReader = std::function<bool(std::uint32_t, std::span<std::uint8_t, 2048>)>;
  // Told the SHA-256 hex of a payload the transfer has just read, before it is written to RAM and
  // published as an image. The ledger that records operations uses this instead of hashing the
  // same bytes a second time: two digests over one payload are two facts that can disagree, and
  // the one that names the image is the one the guest's code will execute.
  using PayloadObserver = std::function<void(std::string_view)>;

  archive_transfer::Decision read(Core &core,
                                  const ArchiveRead &request,
                                  bool deferred,
                                  const PayloadObserver &onPayload = {});
  archive_transfer::Decision read(Core &core,
                                  const ArchiveRead &request,
                                  bool deferred,
                                  const SectorReader &reader,
                                  const PayloadObserver &onPayload = {});
  bool takeCompletion();

private:
  bool completionPending_ = false;
};

} // namespace spyro
