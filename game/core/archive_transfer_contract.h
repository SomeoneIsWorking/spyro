// archive_transfer_contract.h — the completion boundary for Spyro's WAD reads.
#pragma once

#include <cstdint>

namespace spyro::archive_transfer {

struct Evidence {
  std::uint32_t requestedBytes = 0;
  std::uint32_t movedBytes = 0;

  constexpr bool complete() const {
    return movedBytes == requestedBytes;
  }

  // Transfers are sequential from byte zero. This half-open bound states exactly which source
  // bytes are proven to have reached guest RAM; it lets a caller distinguish a missing tail from a
  // later writer without inferring coverage from a success return.
  constexpr std::uint32_t coveredEnd() const {
    return movedBytes;
  }
};

constexpr Evidence evidence(std::uint32_t requestedBytes, std::uint32_t movedBytes) {
  return {.requestedBytes = requestedBytes, .movedBytes = movedBytes};
}

} // namespace spyro::archive_transfer
