// native_render.cpp — CAN the per-call differential validate a geometry renderer at all?
//
// THE QUESTION, ASKED BEFORE THE EXPENSIVE WORK. Widescreen and 60fps both require owning this
// game's hand-written assembly renderers (re-frontier: render.own-geometry-family), and every owned
// body in this port is admitted only when ndiff proves it byte-identical to the body it replaces. A
// byte-exact reimplementation of a 278-instruction assembly renderer is days of work whose payoff is
// invisible until it is finished — so the first thing to establish is whether the ACCEPTANCE TEST
// even works on a function of this shape. If it cannot, the whole plan needs rethinking, and it is
// far cheaper to learn that now.
//
// THE EXPERIMENT IS AN IDENTITY: hand ndiff the generated body as BOTH the "native" replacement and
// the substrate reference. It runs the body, rewinds RAM + scratchpad + all GPRs + the COP2 register
// file, runs the same body again, and compares. A correct harness on a deterministic function MUST
// report a match. Anything else is the harness or the function telling us this validation route is
// closed — for instance a body that reads state the rewind does not restore (host GPU state, a
// timer, an ordering-table pointer living outside guest RAM), which is exactly the hazard the
// re-frontier already records for spin-loop bodies.
//
// Why 0x8004EBA8: it is the one renderer understood at instruction level end to end (two stages,
// 11/11/10-bit packed vertex deltas, a scratchpad vertex cache indexed by pre-scaled byte offsets
// from the face list, POLY_FT3 at stride 0x1C and F3 at 0x14). It also only WRITES packets into
// guest RAM — the DMA to the GPU happens later, from a different call — so running it twice has no
// host-side effect the rewind would fail to undo. A renderer that submitted to the GPU directly
// could not be tested this way.
//
// TEMPORARY, and gated: this is a measurement, not ownership. It installs nothing on a normal run.
#include "core.h"
#include "recomp_iface.h"
#include "rec_decls.h"
#include "native_diff.h"
#include "cfg.h"
#include "spyro_game.h"
#include <cstdio>
#include <cstdlib>

namespace {

// ANY address, not one hardcoded body — there are eight clip-bound renderers to own and each needs
// this asked of it before the transcription starts. The generated body cannot be named generically, so
// the probe re-dispatches: it steps out of its own override slot, dispatches the address (which now
// finds no override and runs the real body), and puts itself back. Same self-clearing trampoline
// fntrace uses, and for the same reason.
uint32_t s_addr = 0;
char s_name[64];
void ident_hook(Core* c);

void redispatch(Core* c) {
  const RecompRegistry* R = psxport_recomp();
  R->shard_set_override(s_addr, nullptr);
  R->main_dispatch(c, s_addr);
  R->shard_set_override(s_addr, ident_hook);
}

// ndiff calls `native` first, rewinds, then calls `body`; handing it the SAME function twice asks only
// "is this function reproducible under the rewind?" — which is what has to be true before a
// reimplementation of it could ever be certified.
void ident_hook(Core* c) {
  s_addr = c->pc;
  ndiff_run(c, s_name, redispatch, redispatch);
}

}  // namespace

void spyro_register_native_render() {
  // PSXPORT_NDIFF_IDENTITY=<hex guest address> — off unless asked for. Running any body twice per
  // call is far too expensive for a normal run, and this answers a one-off question per renderer.
  const char* e = cfg_str("PSXPORT_NDIFF_IDENTITY");
  if (!e || !*e) return;
  const uint32_t addr = (uint32_t)strtoul(e, nullptr, 16);
  if (!addr) {
    cfg_loge("ndiff", "PSXPORT_NDIFF_IDENTITY=%s is not a hex guest address (e.g. 8004F000)", e);
    return;
  }
  snprintf(s_name, sizeof s_name, "IDENTITY@0x%08X", addr);
  cfg_logi("ndiff", "%s ARMED — running the generated body against itself. A divergence means the "
                    "differential CANNOT validate a function of this shape, and owning it would need "
                    "a different acceptance test.", s_name);
  psxport_recomp()->shard_set_override(addr, ident_hook);
}
