#pragma once

#include <cstdint>
#include <lucent/log.h>
#include <string>
#include <utility>

namespace spyro {

// Why a native producer would not draw this frame. The message is built once, at the site that
// knows what it saw, and travels to the abort: one only a debug channel can explain is not usable.
struct ProducerRefusal {
  uint32_t producer = 0; // the guest producer that refused, or 0 when the composition succeeded
  std::string detail; // what it saw, in its own terms; empty only for a layer with nothing to add

  explicit operator bool() const {
    return producer != 0;
  }
};

// Build a refusal and log it on `channel` in the same breath, so the two can never disagree.
template <typename... Args>
ProducerRefusal refuse(const char *channel,
                       uint32_t producer,
                       lucent::detail::FormatString<Args...> format,
                       Args &&...args) {
  std::string detail = lucent::format(format, std::forward<Args>(args)...);
  lucent::debug(channel, "REFUSED {}", detail);
  return ProducerRefusal{producer, std::move(detail)};
}

} // namespace spyro
