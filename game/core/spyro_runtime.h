#pragma once

#include "game_runtime.h"
#include "title_logo_facts.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace spyro {

enum class SpyroTitle : std::uint8_t { Spyro1, Spyro2, Spyro3 };

// Runtime root; each serial owns its immutable executable image and behavior in a derived title
// runtime.
class SpyroRuntime : public GameRuntime {
public:
  const GuestProgramImage *guestProgramImage() const final;
  SpyroTitle title() const;

  // Where the title's own logo lands in VRAM and how the title encodes it are facts about that
  // disc's title, not about this runtime: a derived title answers with its own measured numbers and
  // the base answers with none.
  virtual const TitleLogoFacts &logoFacts() const;

  std::optional<psx::host::LogoImage> panelLogo(Core &core) const final;
  void reportRun(Core &core) const final;

  // A title that has one overrides both of these. Reporting widescreen only and building no
  // presentation keeps the product at the guest's real frame rate, which is the truth about a frame
  // that has no in-between rather than a claim of 60 fps over an empty one.
  RenderCapabilities renderCapabilities() const override;

protected:
  SpyroRuntime(const GuestProgramImage &programImage, SpyroTitle title);

private:
  const GuestProgramImage &programImage_;
  SpyroTitle title_;
};

} // namespace spyro
