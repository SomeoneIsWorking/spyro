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

namespace {

// The generated body serves as both sides. ndiff calls `native` first, rewinds, then calls `body`;
// passing the same function to each asks only "is this function reproducible under the rewind?".
void terrain_identity(Core* c) {
  ndiff_run(c, "IDENTITY terrain@0x8004EBA8", gen_func_8004EBA8, gen_func_8004EBA8);
}

}  // namespace

void spyro_register_native_render() {
  // PSXPORT_NDIFF_IDENTITY=1 — off by default. Running any body twice per call is far too expensive
  // for a normal run, and this answers a one-off question.
  if (!cfg_on("PSXPORT_NDIFF_IDENTITY")) return;
  cfg_logi("ndiff", "IDENTITY PROBE ARMED on 0x8004EBA8 — running the generated body against itself. "
                    "A divergence here means the differential CANNOT validate a renderer of this "
                    "shape, and the ownership plan needs a different acceptance test.");
  psxport_recomp()->shard_set_override(0x8004EBA8u, terrain_identity);
}
