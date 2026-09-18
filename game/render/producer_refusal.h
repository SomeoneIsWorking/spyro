#pragma once

#include <cstdint>
#include <lucent/log.h>
#include <string>
#include <utility>

namespace spyro {

// Why a native producer would not draw this frame.
//
// WHY THIS EXISTS. The fatal render boundary used to print only WHICH producer refused. The reason
// existed, but only as a `lucent::debug` line on a channel that is off by default, so the abort a
// user or an unattended run actually sees named a guest address and nothing else. Issue 0113 sat
// open for four days as "not yet deterministic" for exactly that: the run that reproduced it had no
// debug channels, and the reason was gone by the time anyone read the backtrace.
//
// So the message is built once, at the site that knows what it saw, and travels to the abort. A
// refusal that only a debug channel can explain is not one this project ships.
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
