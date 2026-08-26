#pragma once

#include "game_runtime.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace spyro {

enum class SpyroTitle : std::uint8_t { Spyro1, Spyro2, Spyro3 };

struct ExecutableIdentity {
  SpyroTitle title;
  std::string_view displayName;
  std::string_view serial;
  std::size_t fileSize;
  std::string_view sha256;
  std::uint32_t entry;
  std::uint32_t globalPointer;
  std::uint32_t textAddress;
  std::uint32_t textSize;
  std::uint32_t stackAddress;
  std::uint32_t stackOffset;
};

// Engine-lineage runtime root. Each serial owns its immutable executable image and behavior in a
// derived title runtime; this base prevents one title's GameConfig from becoming another title's
// identity by accident.
class SpyroRuntime : public GameRuntime {
public:
  const GuestProgramImage *guestProgramImage() const final;
  SpyroTitle title() const;

  // An identified lineage title does not acquire Spyro 1's renderer merely by deriving from this
  // base. Titles without an installed substrate refuse before Game construction; these facts keep
  // the framework from advertising native or temporal products if that boundary is later reached.
  RenderCapabilities renderCapabilities() const override {
    return {
        .defaultPath = RenderPath::Gte,
        .nativeRenderPath = false,
        .temporalInterpolation = false,
    };
  }

  // A selected title owns its substrate decision. Returning false is an honest pre-Game refusal:
  // the title was identified, but this repository cannot execute it yet.
  virtual bool installSubstrate() = 0;
  virtual std::string_view substrateRefusal() const = 0;

protected:
  SpyroRuntime(const GuestProgramImage &programImage, SpyroTitle title);

private:
  const GuestProgramImage &programImage_;
  SpyroTitle title_;
};

} // namespace spyro
