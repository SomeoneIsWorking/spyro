#pragma once

#include "archive_transfer_contract.h"

#include <cstdint>
#include <functional>
#include <span>

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

  archive_transfer::Decision read(Core &core, const ArchiveRead &request, bool deferred);
  archive_transfer::Decision
  read(Core &core, const ArchiveRead &request, bool deferred, const SectorReader &reader);
  bool takeCompletion();

private:
  bool completionPending_ = false;
};

} // namespace spyro
