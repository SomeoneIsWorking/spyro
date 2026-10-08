#include "spyro1_press_latch.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "press_latch: FAIL: " << what << "\n";
    std::exit(1);
  }
}

// The boot sequence's shape: one observe per delivered field, and a take only in a hold phase.
// `fieldsBeforeHold` fields pass (fades and the loader), the press lands on field `pressAt` when it
// is non-negative, and the hold then asks once. Returns what the hold was told.
bool holdSkips(int fieldsBeforeHold, int pressAt) {
  spyro1::PressLatch latch;
  for (int field = 0; field < fieldsBeforeHold; ++field) {
    latch.observe(field == pressAt);
  }
  return latch.take();
}

void testNoPressNoSkip() {
  require(!holdSkips(16, -1), "a hold reached without any press must not skip");
}

void testPressInAFadeSkipsAtTheHold() {
  require(holdSkips(16, 0), "a press on the first fade field is honoured at the hold");
  require(holdSkips(16, 7), "a press on the last fade field is honoured at the hold");
  require(holdSkips(16, 15), "a press on the loader's last field is honoured at the hold");
}

void testPressOnTheHoldFieldItself() {
  spyro1::PressLatch latch;
  latch.observe(true);
  require(latch.take(), "a press on the hold's own field skips, as it did without the latch");
}

void testOnePressIsTakenOnce() {
  spyro1::PressLatch latch;
  latch.observe(true);
  require(latch.take(), "the held press is taken");
  require(!latch.take(), "the same press is not taken twice");
  latch.observe(false);
  require(!latch.take(), "a field without a press does not revive it");
}

void testAQuietFieldKeepsAHeldPress() {
  spyro1::PressLatch latch;
  latch.observe(true);
  for (int field = 0; field < 100; ++field) {
    latch.observe(false);
  }
  require(latch.held(), "a press is held across fields with no press");
  require(latch.take(), "and is honoured when the hold is reached");
}

void testTwoPressesAreTwoSkips() {
  spyro1::PressLatch latch;
  latch.observe(true);
  require(latch.take(), "first hold skipped");
  latch.observe(false);
  latch.observe(true);
  require(latch.take(), "a second press skips the second hold");
  require(!latch.take(), "and nothing is left over");
}

} // namespace

int main() {
  testNoPressNoSkip();
  testPressInAFadeSkipsAtTheHold();
  testPressOnTheHoldFieldItself();
  testOnePressIsTakenOnce();
  testAQuietFieldKeepsAHeldPress();
  testTwoPressesAreTwoSkips();
  std::cout << "press_latch: PASS (a press made before a hold is honoured once, never invented)\n";
  return 0;
}
