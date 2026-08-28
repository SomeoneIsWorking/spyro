// Binary-derived setup facts for libmcrd's asynchronous request starters.
#pragma once

#include <cstdint>

namespace spyro {

enum class MemcardOperation : uint32_t {
  Exist = 1u,
  Accept = 2u,
};

struct MemcardOperationPlan {
  MemcardOperation operation;
  uint32_t callback;
};

constexpr uint32_t kMemcardPendingOperation = 0x80075B50u;
constexpr uint32_t kMemcardPhase = 0x80075B54u;
constexpr uint32_t kMemcardResult = 0x80075B58u;
constexpr uint32_t kMemcardRequestArgument = 0x80075B5Cu;
constexpr uint32_t kMemcardExistCallback = 0x800663D8u;
constexpr uint32_t kMemcardAcceptCallback = 0x80066634u;

// The two public starters share one libmcrd contract: accept only an idle request and register
// the matching state-machine callback. The callbacks are guest addresses because the event stack
// dispatches them later through the recompiled substrate.
constexpr MemcardOperationPlan memcardOperationPlan(MemcardOperation operation) {
  return {
      .operation = operation,
      .callback =
          operation == MemcardOperation::Exist ? kMemcardExistCallback : kMemcardAcceptCallback,
  };
}

} // namespace spyro
