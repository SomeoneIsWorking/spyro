#include "title_availability.h"

#include "title_selection.h"

#include <utility>

namespace spyro {

TitleAvailabilityProbe::TitleAvailabilityProbe(std::filesystem::path provisioningRoot,
                                               std::span<const ExecutableIdentity> catalog)
    : root_(std::move(provisioningRoot)), catalog_(catalog) {}

std::vector<TitleAvailability> TitleAvailabilityProbe::probe() const {
  std::vector<TitleAvailability> titles;
  titles.reserve(catalog_.size());
  for (const ExecutableIdentity &identity : catalog_) {
    TitleAvailability entry;
    entry.identity = &identity;
    entry.executable = root_ / std::string(identity.slug) / std::string(identity.serial);
    const SelectionResult selection = selectExecutableFile(entry.executable, catalog_);
    switch (selection.status) {
    case SelectionStatus::Selected:
      entry.status = AvailabilityStatus::Available;
      break;
    case SelectionStatus::MissingExecutable:
      entry.status = AvailabilityStatus::NotProvisioned;
      entry.reason = "Not provisioned";
      break;
    case SelectionStatus::UnsupportedSerial:
    case SelectionStatus::InvalidExecutable:
    case SelectionStatus::IdentityMismatch:
      entry.status = AvailabilityStatus::IdentityMismatch;
      entry.reason =
          "Identity mismatch: the provisioned executable is not " + std::string(identity.serial);
      break;
    }
    titles.push_back(std::move(entry));
  }
  return titles;
}

} // namespace spyro
