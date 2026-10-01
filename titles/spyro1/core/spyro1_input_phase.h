// spyro1_input_phase.h — the input phase a Spyro 1 pad recording is keyed on.
//
// WHY A TITLE HAS TO SUPPLY THIS. A .pad recording used to be one mask per pad frame counted from
// boot, so every press landed on the absolute frame it was captured on. Anything that moves the
// frame count — a slower CD read, a different enhancement configuration, a build that presents one
// more boot frame — shifts every press after it, and the recording then answers a screen it was
// never recorded against. Spyro's only gameplay replay did exactly that (docs/issues/0116).
//
// The fix is to key each recorded frame on WHICH SCREEN was taking input, and to store it as an
// offset from that screen's entry. Boot and load timing then move the phase boundary instead of the
// presses inside it. The framework compares phases and records when they change; it does not know
// what a phase means, so the vocabulary is this file's.
//
// THE KEY. `g_Gamestate` (the main loop's own dispatch) packed with `g_LevelId` (which level is
// resident). Two words, and both are already named once in `game/core/guest_globals.h`, which is
// also what `tools/guest_globals.py` hands the agent drivers — so this owner, the product and
// `tools/drive.py` read the same memory by construction rather than by three spellings agreeing.
//
// WHY NOT MORE WORDS. The title-screen menu is several screens inside one gamestate, and a finer
// key would be more robust. It is deliberately not: every word added is a word that must hold still
// for as long as the screen is, and `g_TitlescreenState` carries two free-running TICK COUNTERS
// beside its mode and state (see tools/title_states.py). A key built from a counter re-keys every
// frame and makes the recording describe a moving target. See `describe()` for the named view a log
// line gets.
#pragma once

#include "game_runtime.h"
#include "input_phase.h"

#include <cstdint>
#include <string>

class Core;

namespace spyro1 {

// The Spyro 1 pad-replay phase. Stateless by construction — every answer is read from the Core —
// and held by value in the runtime so no process-global state can select a different instance.
class InputPhase {
public:
  // The phase for this pad frame. A title declares no phase at all by answering
  // psx::input::kUnkeyedPhase (GameRuntime's default); a title that declares one must never produce
  // that value, so the halves are masked and `of` asserts the property rather than trusting it.
  std::uint64_t of(Core &core) const;

  // How a phase reads in a log line and in a replay report: "gs=0/level=0x0c" for a real key,
  // "unkeyed" for the reserved one. This is the NARROWER question the framework reports — which
  // (gamestate, level) pair — not "which screen", because the pair is what the recording is keyed
  // on.
  static std::string describe(std::uint64_t phase);

  // The packed form, exposed so a test can cross-check it against the header's addresses without
  // restating the packing. `gamestate` and `level` are each masked to 16 bits, which is more than
  // both words need (the widest gamestate tools/title_states.py names is 15) and keeps the key 32
  // bits wide, so it can never collide with psx::input::kUnkeyedPhase.
  static constexpr std::uint64_t pack(std::uint32_t gamestate, std::uint32_t level) {
    return (static_cast<std::uint64_t>(gamestate & 0xFFFFu) << 16) | (level & 0xFFFFu);
  }
  static constexpr std::uint32_t gamestateOf(std::uint64_t phase) {
    return static_cast<std::uint32_t>((phase >> 16) & 0xFFFFu);
  }
  static constexpr std::uint32_t levelOf(std::uint64_t phase) {
    return static_cast<std::uint32_t>(phase & 0xFFFFu);
  }
};

} // namespace spyro1
