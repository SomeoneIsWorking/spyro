#pragma once

#include "game_runtime.h"
#include "title_logo_facts.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace spyro {

enum class SpyroTitle : std::uint8_t { Spyro1, Spyro2, Spyro3 };

// WHETHER A TITLE'S OWN GUEST IS IN ITS ATTRACT DEMO, OR THE PORT CANNOT SAY. `Unknown` is not a
// maybe: it is the honest answer for a title that has not measured its own demo state, and the
// panel rules key on it by falling back to the older "freeze on the first real picture" rule rather
// than making a claim that title's evidence does not support.
enum class AttractState : std::uint8_t { Unknown, NotInDemo, InDemo };

struct ExecutableIdentity {
  SpyroTitle title;
  std::string_view displayName;
  std::string_view serial;
  // Directory name under the provisioning root where this title's executable lands
  // (`scratch/assets/<slug>/`); tools/generate_title_catalog.py's TITLE_ENUMS is the one source,
  // shared with tools/provision_title.py.
  std::string_view slug;
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

  // WHERE THIS TITLE'S OWN LOGO IS. The wordmark belongs to the player's disc, and where it lands
  // in VRAM and how the title encodes it are facts about that disc's title, not about this runtime:
  // a derived title answers with its own measured numbers (its `*_logo_facts.h`) and the base
  // answers with none, so a title that has not been reverse-engineered shows no name rather than
  // someone else's. The host asks through this, not through a `void *`: the type is this project's
  // and the question is answered by the title that has the facts for it.
  virtual const TitleLogoFacts &logoFacts() const;

  // WHETHER THE GUEST IS IN ITS OWN ATTRACT DEMO RIGHT NOW. The picker's panels pre-roll until this
  // says InDemo and then freeze, so a panel never holds a publisher card or a loading screen and
  // calls it a demo. Also per title: each of the three has its own runtime state for it, and one
  // that has not been reverse-engineered answers Unknown, which falls the panel back to the older
  // rule (freeze on its first real picture) rather than making a claim it cannot support.
  virtual AttractState attractState(const Game &game, Core &core) const;

  // An identified lineage title does not acquire Spyro 1's renderer merely by deriving from this
  // base. Runtime execution always consumes the selected executable directly through Lightrec.
  //
  // This base also declares no temporal product of its own: a title that has one overrides both of
  // these. Reporting widescreen only and building no presentation keeps the product at the guest's
  // real frame rate, which is the truth about a frame that has no in-between rather than a claim
  // of 60 fps over an empty one.
  RenderCapabilities renderCapabilities() const override;

protected:
  SpyroRuntime(const GuestProgramImage &programImage, SpyroTitle title);

private:
  const GuestProgramImage &programImage_;
  SpyroTitle title_;
};

} // namespace spyro
