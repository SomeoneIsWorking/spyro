#pragma once

#include "spyro_runtime.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace spyro {

enum class SelectionStatus : std::uint8_t {
  Selected,
  UnsupportedSerial,
  MissingExecutable,
  InvalidExecutable,
  IdentityMismatch,
};

struct SelectionResult {
  SelectionStatus status;
  const ExecutableIdentity *identity;
  std::string detail;

  explicit operator bool() const {
    return status == SelectionStatus::Selected;
  }
};

SelectionResult selectExecutable(std::string_view serial,
                                 std::span<const std::uint8_t> bytes,
                                 std::span<const ExecutableIdentity> catalog);
SelectionResult selectExecutableFile(const std::filesystem::path &path,
                                     std::span<const ExecutableIdentity> catalog);

} // namespace spyro
