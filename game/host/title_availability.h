#pragma once

#include "spyro_runtime.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace spyro {

enum class AvailabilityStatus : std::uint8_t {
  Available,
  NotProvisioned,
  IdentityMismatch,
};

// One catalog title as the selector may offer it. `identity` is the catalog entry (serial and
// hashes): that, not the label, is what a selection means.
struct TitleAvailability {
  const ExecutableIdentity *identity = nullptr;
  std::filesystem::path executable;
  AvailabilityStatus status = AvailabilityStatus::NotProvisioned;
  std::string reason; // empty when Available

  bool available() const {
    return status == AvailabilityStatus::Available;
  }
};

// Decides which catalog titles can be started: the executable must be provisioned at
// `<root>/<slug>/<serial>` AND authenticate through the same `selectExecutableFile` the boot path
// uses. A title that is not provisioned never blocks one that is.
class TitleAvailabilityProbe {
public:
  TitleAvailabilityProbe(std::filesystem::path provisioningRoot,
                         std::span<const ExecutableIdentity> catalog);

  // One entry per catalog title, in catalog order.
  std::vector<TitleAvailability> probe() const;

private:
  std::filesystem::path root_;
  std::span<const ExecutableIdentity> catalog_;
};

} // namespace spyro
