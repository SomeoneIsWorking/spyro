#include "boot_skip.h"
#include <lucent/log.h>

int spyro_boot_skip_selftest() {
  int checks = 0;
  bool ok = true;
  auto expect = [&](bool pass, const char *what) {
    ++checks;
    if (!pass) {
      lucent::error("selftest", "FAIL(bootskip): {}", what);
    }
    ok &= pass;
  };
  BootSkipState s;
  expect(boot_skip_sample(s, true) == BootSkipAction::None, "inactive ignores Start");
  boot_skip_begin(s);
  expect(boot_skip_sample(s, true) == BootSkipAction::Baseline, "held entry is baseline");
  expect(boot_skip_sample(s, true) == BootSkipAction::None, "continued hold is ignored");
  expect(boot_skip_sample(s, false) == BootSkipAction::None, "release is ignored");
  expect(boot_skip_sample(s, true) == BootSkipAction::AdvancePresentation, "fresh edge advances");
  expect(boot_skip_sample(s, true) == BootSkipAction::None, "held edge cannot repeat");
  expect(s.fields == 5 && s.edges == 1 && s.advances == 1, "denominators cover both answers");
  boot_skip_begin(s);
  expect(boot_skip_sample(s, false) == BootSkipAction::Baseline, "released entry is baseline");
  expect(boot_skip_sample(s, true) == BootSkipAction::AdvancePresentation,
         "released baseline then press advances");
  if (ok) {
    lucent::info("selftest", "PASS(bootskip): {} checks", checks);
  }
  return ok ? 0 : 1;
}
