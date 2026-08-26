#include "memcard_event_stack.h"

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "memcard_event_stack: %s\n", what);
    std::abort();
  }
}

} // namespace

int main() {
  constexpr spyro::MemcardEventPushPlan first = spyro::memcardEventPushPlan(0xFFFFFFFFu);
  require(first.index == 0u && first.hasCapacity, "reset index did not select the first entry");
  require(first.stateBase == 0x80075C08u && first.handlerSlot == 0x80075C48u,
          "first state and handler addresses differ from 0x80068F44");

  constexpr spyro::MemcardEventPushPlan signedNegative = spyro::memcardEventPushPlan(0xFFFFFFFEu);
  require(signedNegative.index == 0xFFFFFFFFu && signedNegative.hasCapacity,
          "the executable's signed slti acceptance was replaced with an unsigned bound");

  constexpr spyro::MemcardEventPushPlan fourth = spyro::memcardEventPushPlan(2u);
  require(fourth.index == 3u && fourth.hasCapacity, "index three was not accepted");
  require(fourth.stateBase == 0x80075C38u && fourth.handlerSlot == 0x80075C54u,
          "fourth entry did not apply the executable's 16-byte/4-byte strides");

  constexpr spyro::MemcardEventPushPlan overflow = spyro::memcardEventPushPlan(3u);
  require(overflow.index == 4u && !overflow.hasCapacity,
          "the fifth entry no longer takes libmcrd's overflow path");
  return 0;
}
