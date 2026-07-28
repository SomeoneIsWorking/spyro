// level_load_probe.cpp — where does the level-overlay load stop happening?
//
// THE QUESTION. The port dies calling a level HANDLER whose overlay was never loaded (claim C039,
// docs/issues/0017). The load path exists and is statically reachable from main:
//
//   main 0x80012204 -> 0x8003385C -> 0x8002EDF0 -> 0x800144C8 -> the loader 0x80016500
//
// and the call at 0x80012230 is UNCONDITIONAL. Yet a whole run makes only six loader calls and none of
// them is a level. So the chain is entered and abandoned somewhere; these probes say where.
//
// WHY PROBES RATHER THAN MORE READING. The static chain shows what CAN happen. Which branch is taken
// depends on runtime state, and this session's repeated lesson is that runtime questions get runtime
// answers — reading further would be guessing at guard values I can simply observe.
//
// 0x800144C8's arguments are logged because they are the load itself: it indexes a table at
// 0x8007A720 by [0x80075964] << 4 to get the WAD offset, and takes its destination from [0x800785E4].
// Both are .bss, so both are runtime values, and a zero in either is a complete explanation on its own.
//
// TEMPORARY. Remove once the load fires; these are super-calls and change no behaviour.
#include "core.h"
#include "game.h"
#include "cfg.h"
#include "recomp_iface.h"
#include "rec_decls.h"
#include "spyro_game.h"

namespace {

// Log the first few entries only: the question is "is it reached at all", and an unbounded per-frame
// log buries the answer it is meant to surface.
// main's stage dispatcher. Its whole body is a switch on the MODE global [0x800757D8]:
//   0 -> 0x80033A54   1 -> 0x8002DF9C   2 -> 0x8002E12C   3 -> 0x8002EB2C
//   4 or 5 -> 0x8002EDF0  (the branch that reaches the level overlay load)   6 -> ...
// So "does the level load" reduces to "does the mode ever reach 4". Log every mode CHANGE rather than
// every entry: this runs per stage tick, and the transitions are the signal.
void probe_8003385C(Core* c) {
  static uint32_t last = 0xFFFFFFFFu;
  static unsigned changes = 0;
  const uint32_t mode = c->mem_r32(0x800757D8u);
  if (cfg_dbg("lvl") && mode != last && changes < 24) {
    cfg_logf("lvl", "stage mode [0x800757D8] %u -> %u%s", last, mode,
             (mode == 4 || mode == 5) ? "   <== the level-load branch" : "");
    last = mode; changes++;
  }
  gen_func_8003385C(c);
}

void probe_8002EDF0(Core* c) {
  static unsigned n = 0;
  if (cfg_dbg("lvl") && n < 4) cfg_logf("lvl", "enter 0x8002EDF0 (stage setup) #%u", n);
  n++;
  gen_func_8002EDF0(c);
}

// The level load itself. Its inputs decide whether the loader call that follows is meaningful:
//   level index [0x80075964], the offset table at 0x8007A720 (stride 16), destination [0x800785E4],
//   and the length/base globals the disassembly at 0x800144E0-0x80014500 reads.
void probe_800144C8(Core* c) {
  static unsigned n = 0;
  if (cfg_dbg("lvl") && n < 6) {
    const uint32_t idx  = c->mem_r32(0x80075964u);
    const uint32_t dest = c->mem_r32(0x800785E4u);
    const uint32_t off  = c->mem_r32(0x80080600u);
    const uint32_t len  = c->mem_r32(0x80080604u);
    const uint32_t ent  = c->mem_r32(0x8007A720u + (idx << 4));
    cfg_logf("lvl", "enter 0x800144C8 (LEVEL LOAD) #%u  idx=%u tbl[idx]=0x%08X off=0x%08X "
                    "len=%u dest=0x%08X",
             n, idx, ent, off, len, dest);
  }
  n++;
  gen_func_800144C8(c);
}

}  // namespace

void spyro_register_level_probes() {
  psxport_recomp()->shard_set_override(0x8003385Cu, probe_8003385C);
  psxport_recomp()->shard_set_override(0x8002EDF0u, probe_8002EDF0);
  psxport_recomp()->shard_set_override(0x800144C8u, probe_800144C8);
}
