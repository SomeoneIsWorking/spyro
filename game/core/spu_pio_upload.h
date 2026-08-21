// Binary-derived batching rules for PsyQ WriteSpuRamPio (0x8005BE88).
#pragma once

#include <cstdint>

namespace spyro {

constexpr uint32_t kSpuPioBatchBytes = 0x40u;

constexpr uint32_t spuPioBatchBytes(uint32_t remaining) {
  return remaining < kSpuPioBatchBytes + 1u ? remaining : kSpuPioBatchBytes;
}

constexpr uint32_t spuPioHalfwordCount(uint32_t bytes) {
  return bytes / 2u + bytes % 2u;
}

} // namespace spyro
