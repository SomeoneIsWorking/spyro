#include "title_menu_oracle.h"

#include "cfg.h"
#include "core.h"
#include "ov_ov_5b800_decls.h"
#include "title_menu_recipe.h"
#include "title_menu_state.h"

#include <cstdlib>
#include <lucent/log.h>

namespace {

using spyro::title_menu_recipe::Recipe;

bool sCapturing = false;
Recipe sObserved;
uint64_t sComparedCalls = 0;

void spriteEmitterOracle(Core *core) {
  if (sCapturing) {
    if (sObserved.size >= sObserved.commands.size()) {
      lucent::error("titleoracle",
                    "DIVERGES: guest emitted more than {} mode-1 sprite commands",
                    sObserved.commands.size());
      std::abort();
    }
    sObserved.append(static_cast<int32_t>(core->r[4]),
                     static_cast<int32_t>(core->r[5]),
                     static_cast<int32_t>(core->r[6]),
                     core->r[7]);
  }
  ov_ov_5b800_gen_8007CD38(core);
}

void titleDrawOracle(Core *core) {
  const auto state = spyro::title_menu_state::read(core);
  if (state.mode != 1u) {
    ov_ov_5b800_gen_8007CEE4(core);
    return;
  }

  const Recipe expected = spyro::title_menu_recipe::buildMode1(state.mode1Input());
  sObserved = {};
  sCapturing = true;
  ov_ov_5b800_gen_8007CEE4(core);
  sCapturing = false;

  size_t mismatch = 0;
  if (!spyro::title_menu_recipe::sameCommands(expected, sObserved, mismatch)) {
    if (mismatch < expected.size && mismatch < sObserved.size) {
      const auto &want = expected.commands[mismatch];
      const auto &got = sObserved.commands[mismatch];
      lucent::error("titleoracle",
                    "DIVERGES call={} substate={} anim={} option={} card={} index={} "
                    "expected_count={} observed_count={} expected=({},{},{},{}) "
                    "observed=({},{},{},{})",
                    sComparedCalls + 1u,
                    state.page,
                    state.anim,
                    state.optionSelected,
                    state.cardSelected,
                    mismatch,
                    expected.size,
                    sObserved.size,
                    want.x,
                    want.y,
                    want.sprite,
                    want.style,
                    got.x,
                    got.y,
                    got.sprite,
                    got.style);
    } else {
      lucent::error("titleoracle",
                    "DIVERGES call={} substate={} anim={} option={} card={} after shared "
                    "prefix={} expected_count={} observed_count={}",
                    sComparedCalls + 1u,
                    state.page,
                    state.anim,
                    state.optionSelected,
                    state.cardSelected,
                    mismatch,
                    expected.size,
                    sObserved.size);
    }
    std::abort();
  }
  ++sComparedCalls;
  lucent::debug("titleoracle",
                "PASS call={} substate={} anim={} option={} card={} commands={}",
                sComparedCalls,
                state.page,
                state.anim,
                state.optionSelected,
                state.cardSelected,
                expected.size);
}

} // namespace

void spyro_register_title_menu_oracle() {
  if (!cfg_on("PSXPORT_TITLE_MENU_ORACLE")) {
    return;
  }
  ov_ov_5b800_set_override(0x8007CD38u, spriteEmitterOracle);
  ov_ov_5b800_set_override(0x8007CEE4u, titleDrawOracle);
  lucent::info("titleoracle",
               "ARMED: retained OV_5B800 0x8007CEE4 mode-1 calls will compare their ordered "
               "0x8007CD38 argument stream with the native title-menu recipe");
}
