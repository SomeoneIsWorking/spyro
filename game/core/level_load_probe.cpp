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
  static uint64_t last = ~0ull;
  static unsigned changes = 0;
  const uint32_t mode = c->mem_r32(0x800757D8u);
  // Mode 13's arm reads [0x80078D78] and calls 0x8007ABAC (OVL0) unless it is 3, in which case it calls
  // 0x80032B08 — so that global is the SUB-STATE inside the stuck mode and is the more likely thing
  // actually failing to advance. [0x800758CC] is the handler pointer whose stale value is what finally
  // crashes the port (C039), so watching it alongside shows whether it is installed before or after the
  // sub-state moves. Key on the PAIR: a mode that never changes could still hide a live sub-state.
  const uint32_t sub  = c->mem_r32(0x80078D78u);
  const uint32_t hptr = c->mem_r32(0x800758CCu);
  const uint64_t key  = ((uint64_t)mode << 40) ^ ((uint64_t)sub << 8) ^ hptr;
  if (cfg_dbg("lvl") && key != last && changes < 40) {
    cfg_logf("lvl", "stage mode=%u sub[0x80078D78]=%u handler[0x800758CC]=0x%08X%s",
             mode, sub, hptr,
             (mode == 4 || mode == 5) ? "   <== the level-load branch" : "");
    last = key; changes++;
  }
  gen_func_8003385C(c);
}

// The mode-13/sub-3 handler. Its body is the LEVEL-LOAD SETUP: at 0x80032B68 it reads the arena base
// constant [0x800113A0] = 0x8007AA38 and writes it to the arena cursors [0x800785D8]/[0x800785DC], which
// is what func_800144C8 later loads into. So the intro stage does intend to load a level here. Two
// guards can skip it, and their values are the whole question:
//   0x80032B24  [0x80078D78] != 3            -> jump 0x80033170 (wrong sub-state, do nothing)
//   0x80032B38  [0x80078D7C] != 0            -> jump 0x80032CD0 (already started?)
//   0x80032B60  [0x80078D94] != 0            -> jump 0x80032B98 (skip the cursor reset)
void probe_80032B08(Core* c) {
  static unsigned n = 0;
  if (cfg_dbg("lvl") && n < 6)
    cfg_logf("lvl", "enter 0x80032B08 (level-load setup) #%u  sub=%u [0x80078D7C]=%u [0x80078D94]=0x%08X "
                    "cursor=[0x%08X,0x%08X]",
             n, c->mem_r32(0x80078D78u), c->mem_r32(0x80078D7Cu), c->mem_r32(0x80078D94u),
             c->mem_r32(0x800785D8u), c->mem_r32(0x800785DCu));
  n++;
  gen_func_80032B08(c);
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

// The REAL pad decoder, 0x80053C68 (the demo-replay reader is the other one, 0x800539FC — C062).
// It reads the slot-0 packet at 0x800786A0 and publishes, among others:
//   [0x80077384] = pad class  0 none / 1 other / 2 digital (id 0x41) / 3 analog (0x53, 0x73)
//   [0x80077378] = newly-pressed mask,  [0x8007737C] = newly-released,  [0x80077380] = held
// Log the packet ALONGSIDE what the decoder made of it, because the failure this is watching for is
// precisely a disagreement between the two: a packet written into the wrong place, or written with a
// type byte the decoder rejects, both show up as "bytes present, class still 0".
void probe_80053C68(Core* c) {
  static uint64_t last = ~0ull;
  static unsigned n = 0;
  gen_func_80053C68(c);
  if (!cfg_dbg("pad")) return;
  const uint32_t st = c->mem_r8(0x800786A0u), id = c->mem_r8(0x800786A1u);
  const uint32_t b2 = c->mem_r8(0x800786A2u), b3 = c->mem_r8(0x800786A3u);
  const uint32_t cls = c->mem_r32(0x80077384u), pressed = c->mem_r32(0x80077378u);
  const uint64_t key = ((uint64_t)st << 40) ^ ((uint64_t)id << 32) ^ (b2 << 24) ^ (b3 << 16)
                     ^ (cls << 8) ^ pressed;
  static unsigned calls = 0;
  calls++;
  // Log a bounded number of calls UNCONDITIONALLY as well as every state change. Deduping alone
  // cannot distinguish "the state never changed" from "the decoder was only ever called once", and
  // those two have opposite diagnoses.
  const bool change = (key != last);
  last = key;
  if (calls > 8 && (!change || n >= 60)) return;
  if (change) n++;
  cfg_logf("pad", "call #%u packet %02X %02X %02X %02X -> class[0x80077384]=%u "
                  "pressed[0x80077378]=0x%08X%s",
           calls, st, id, b2, b3, cls, pressed,
           cls == 0 ? "   <== decoder says NO CONTROLLER" : "");
}

}  // namespace

void spyro_register_level_probes() {
  psxport_recomp()->shard_set_override(0x80053C68u, probe_80053C68);
  psxport_recomp()->shard_set_override(0x8003385Cu, probe_8003385C);
  psxport_recomp()->shard_set_override(0x8002EDF0u, probe_8002EDF0);
  psxport_recomp()->shard_set_override(0x800144C8u, probe_800144C8);
  psxport_recomp()->shard_set_override(0x80032B08u, probe_80032B08);
}
