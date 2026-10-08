// Binary-derived layout for libmcrd's event-stack push (0x80068F44).
#pragma once

#include <cstdint>

namespace spyro {

constexpr uint32_t kMemcardEventStackIndex = 0x800751B0u;
constexpr uint32_t kMemcardEventStateBase = 0x80075C08u;
constexpr uint32_t kMemcardEventHandlerBase = 0x80075C48u;
constexpr uint32_t kMemcardEventCapacity = 4u;
constexpr uint32_t kMemcardEventStateBytes = 16u;

struct MemcardEventPushPlan {
  uint32_t index;
  bool hasCapacity;
  uint32_t stateBase;
  uint32_t handlerSlot;
};

// SCUS_942.28 0x80068F44..0x80068FC0 increments the signed stack index, rejects index >= 4,
// stores the state-machine handler in a parallel pointer table, and clears one 16-byte state.
constexpr MemcardEventPushPlan memcardEventPushPlan(uint32_t currentIndex) {
  const uint32_t index = currentIndex + 1u;
  const bool hasCapacity = index < kMemcardEventCapacity || index >= 0x80000000u;
  return {
      .index = index,
      .hasCapacity = hasCapacity,
      .stateBase = kMemcardEventStateBase + index * kMemcardEventStateBytes,
      .handlerSlot = kMemcardEventHandlerBase + index * 4u,
  };
}

} // namespace spyro
