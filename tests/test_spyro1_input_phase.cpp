// The Spyro 1 input phase: what it reads, what it refuses to answer, and that it cannot collide
// with the framework's reserved "no phase".
//
// THE NEGATIVE THAT MATTERS. `psx::input::kUnkeyedPhase` means "this title declares no phase", and
// a phase that ever produced it would make a phase-keyed recording replay as absolute-from-boot
// while the file claimed to be keyed — the exact silent-wrong-answer shape issue 0116 is about. So
// the masking is tested, not assumed: 0xFFFFFFFF in both halves must still not produce the reserved
// key.

#include "core.h"
#include "guest_globals.h"
#include "input_phase.h"
#include "spyro1_input_phase.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s\n", what);
    failures++;
  }
}

void checkEq(const std::string &got, const std::string &want, const char *what) {
  if (got != want) {
    std::fprintf(stderr, "FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
    failures++;
  }
}

} // namespace

int main() {
  const spyro1::InputPhase phase;

  // The addresses are the shared owner's, and they are the ones the agent drivers read: this is the
  // cross-check that the product and tools/drive.py look at the same two words.
  check(spyro::guest::kGamestate == 0x800757D8u, "g_Gamestate is the common.h address");
  check(spyro::guest::kLevelId == 0x8007596Cu, "g_LevelId is the common.h address");

  // A real Core over real guest RAM: the phase is a READ of the two words, not a cached decision.
  // ON THE HEAP, and that is load-bearing: Core carries the whole PSX address space, so a `Core` on
  // the stack of a test main() runs the process off the end of the stack instead of reading RAM.
  // This test segfaulted before that was fixed, which is a statement about the test, not the phase.
  auto core = std::make_unique<Core>();
  core->mem_w32(spyro::guest::kGamestate, 13u); // GS_TitleScreen
  core->mem_w32(spyro::guest::kLevelId, 0u);
  check(phase.of(*core) == spyro1::InputPhase::pack(13, 0), "reads gs and level");
  checkEq(spyro1::InputPhase::describe(phase.of(*core)),
          "gs=13/level=0x0",
          "describes the pair by name");

  // A level change is a phase change even inside one gamestate, so a level transition is its own
  // segment rather than an offset in the level before it.
  core->mem_w32(spyro::guest::kLevelId, 0x0Cu);
  check(phase.of(*core) == spyro1::InputPhase::pack(13, 0x0Cu), "a level change re-keys the phase");
  core->mem_w32(spyro::guest::kGamestate, 0u); // GS_Playing
  check(phase.of(*core) == spyro1::InputPhase::pack(0, 0x0Cu), "gamestate is the high half");

  // The same phase for the same words, and a different one for either half moving. This is the
  // property the whole format rests on: two frames with the same key are interchangeable.
  check(spyro1::InputPhase::pack(0, 0x0Cu) == spyro1::InputPhase::pack(0, 0x0Cu), "stable key");
  check(spyro1::InputPhase::pack(0, 1) != spyro1::InputPhase::pack(1, 0), "halves do not alias");

  // The reserved key is unreachable, including from guest RAM that has never been written.
  check(spyro1::InputPhase::pack(0xFFFFFFFFu, 0xFFFFFFFFu) != psx::input::kUnkeyedPhase,
        "0xFFFFFFFF/0xFFFFFFFF does not produce the reserved unkeyed value");
  check(spyro1::InputPhase::pack(0xFFFFFFFFu, 0xFFFFFFFFu) ==
            spyro1::InputPhase::pack(0xFFFFu, 0xFFFFu),
        "and is masked to the two halves the game actually uses");
  const auto virgin = std::make_unique<Core>();
  check(phase.of(*virgin) != psx::input::kUnkeyedPhase,
        "unwritten guest RAM still yields a real key");

  // The reserved key describes itself as unkeyed, so a run that somehow carries it reads correctly
  // instead of printing a hex blob that looks like a real screen.
  checkEq(spyro1::InputPhase::describe(psx::input::kUnkeyedPhase),
          "unkeyed",
          "the reserved key reads as unkeyed");

  if (failures) {
    std::fprintf(stderr, "spyro1_input_phase: %d check(s) failed\n", failures);
    return 1;
  }
  std::printf("spyro1_input_phase: all checks passed\n");
  return 0;
}
