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

void interp_call(Core* c, uint32_t pc);   // interp.cpp — nested call that leaves the guest's ra alone

namespace {

// ANY address, and now ANY NUMBER OF THEM — the remaining ownership queue is five renderers (C133)
// and the question "is this one actually called, and is it reproducible under the rewind?" has to be
// answered for each before choosing which to transcribe. Asking one per run costs a rebuild and a
// capture per address for an answer that a single run can give for all of them, and the arming log
// below prints the whole armed set so a silent typo cannot masquerade as "never called".
//
// The generated body cannot be named generically, so the probe re-dispatches: it steps out of its own
// override slot, dispatches the address (which now finds no override and runs the real body), and puts
// itself back. Same self-clearing trampoline fntrace uses, and for the same reason.
constexpr int kMaxProbes = 16;
uint32_t s_addrs[kMaxProbes];
char s_names[kMaxProbes][64];
int s_count = 0;

// Which address the CURRENTLY EXECUTING probe is for. ndiff calls `redispatch` synchronously from
// inside `ident_hook`, so a single current-address is enough — but it is saved and restored around
// the call because one renderer calling another (both armed) would otherwise leave the outer probe
// re-dispatching the INNER address, which does not fail loudly; it silently runs the wrong body.
uint32_t s_cur = 0;
void ident_hook(Core* c);

void redispatch(Core* c) {
  const RecompRegistry* R = psxport_recomp();
  const uint32_t a = s_cur;
  R->shard_set_override(a, nullptr);
  R->main_dispatch(c, a);
  R->shard_set_override(a, ident_hook);
}

// ndiff calls `native` first, rewinds, then calls `body`; handing it the SAME function twice asks only
// "is this function reproducible under the rewind?" — which is what has to be true before a
// reimplementation of it could ever be certified.
void ident_hook(Core* c) {
  const uint32_t addr = c->pc;
  int idx = -1;
  for (int i = 0; i < s_count; i++) if (s_addrs[i] == addr) { idx = i; break; }
  if (idx < 0) {                     // cannot happen unless the slot was armed for another address
    const RecompRegistry* R = psxport_recomp();
    R->shard_set_override(addr, nullptr);
    R->main_dispatch(c, addr);
    R->shard_set_override(addr, ident_hook);
    return;
  }
  const uint32_t saved = s_cur;
  s_cur = addr;
  ndiff_run(c, s_names[idx], redispatch, redispatch);
  s_cur = saved;
}

// ── MUTE: the one experiment that answers "what does this renderer actually DRAW" without inference.
//
// Twice in this project a renderer's visual contribution was reasoned about and got a wrong answer —
// once badly enough that a working OFX change was recorded as having "no effect" (issue 0039). What
// settled it was replacing the body with nothing and looking at what disappeared. That is a general
// question for every renderer in the ownership queue (which ones draw the 3D world and therefore need
// the projection re-centred, and which draw screen-space content that must NOT move), so it belongs
// here as a facility rather than as a temporary edit to whichever body is under the microscope.
//
// A muted body returns immediately: it writes no packets, links nothing into the ordering table, and
// does not run the register save/restore. That makes it a DIAGNOSTIC ONLY — the guest state it leaves
// behind is not the guest state the real body would leave — so it is loudly logged and never default.
void mute_hook(Core*) {}

// ── INTERPRET: can the flat interpreter stand in for a recompiled renderer, bit for bit?
//
// THE QUESTION BEHIND IT. The widescreen blocker is that every renderer's clip bounds are IMMEDIATE
// constants in its own instruction stream (0x02000000 = sx >= 512), so they cannot be moved while the
// guest owns the code — which is why the plan of record is to transcribe ~9150 instructions of
// hand-written assembly into native C. But the constants are immediates in GUEST RAM too, and the
// interpreter reads them from there rather than from a baked C literal. If interpreting a renderer is
// byte-identical to running its recompiled body, then a widened bound is a one-word change to guest
// memory instead of a thousand lines of transcription, and it stays honest: the code that runs is
// still the game's own, not a reimplementation standing in for it.
//
// This probe asks ONLY the first half — is the interpreted body exact? — because if it is not, the
// rest of the idea is dead and no patching is worth designing. It runs interpreted, then rewinds and
// runs the recompiled body, and reports any difference in RAM, the scratchpad, the GPRs or COP2.
uint32_t s_icur = 0;
char s_inames[kMaxProbes][64];
uint32_t s_iaddrs[kMaxProbes];
int s_icount = 0;

void interp_hook(Core* c);
void interp_side(Core* c) { interp_call(c, s_icur); }

void interp_body(Core* c) {
  const RecompRegistry* R = psxport_recomp();
  const uint32_t a = s_icur;
  R->shard_set_override(a, nullptr);
  R->main_dispatch(c, a);
  R->shard_set_override(a, interp_hook);
}

void interp_hook(Core* c) {
  const uint32_t addr = c->pc;
  int idx = -1;
  for (int i = 0; i < s_icount; i++) if (s_iaddrs[i] == addr) { idx = i; break; }
  if (idx < 0) { interp_call(c, addr); return; }
  const uint32_t saved = s_icur;
  s_icur = addr;
  ndiff_run(c, s_inames[idx], interp_side, interp_body);
  s_icur = saved;
}

}  // namespace

void spyro_register_native_render() {
  // PSXPORT_MUTE_FN=<hex guest address>[,<hex>...] — replace these bodies with nothing.
  if (const char* m = cfg_str("PSXPORT_MUTE_FN")) {
    for (const char* p = m; *p;) {
      while (*p == ',' || *p == ' ') p++;
      if (!*p) break;
      char* end = nullptr;
      const uint32_t addr = (uint32_t)strtoul(p, &end, 16);
      if (end == p) {
        cfg_loge("ndiff", "PSXPORT_MUTE_FN=%s: '%s' is not a hex guest address; NOTHING is muted from "
                          "here on", m, p);
        break;
      }
      p = end;
      if (!addr) continue;
      psxport_recomp()->shard_set_override(addr, mute_hook);
      cfg_logi("ndiff", "MUTE@0x%08X — this body is REPLACED BY NOTHING. Whatever disappears from the "
                        "frame is exactly its visual contribution. The run is diagnostic: guest state "
                        "this body would have written is simply absent.", addr);
    }
  }
  // PSXPORT_INTERP_FN=<hex guest address>[,<hex>...] — run these bodies INTERPRETED, and (under
  // PSXPORT_NDIFF) verify each call against the recompiled body it replaces.
  if (const char* iv = cfg_str("PSXPORT_INTERP_FN")) {
    for (const char* p = iv; *p && s_icount < kMaxProbes;) {
      while (*p == ',' || *p == ' ') p++;
      if (!*p) break;
      char* end = nullptr;
      const uint32_t addr = (uint32_t)strtoul(p, &end, 16);
      if (end == p) {
        cfg_loge("ndiff", "PSXPORT_INTERP_FN=%s: '%s' is not a hex guest address; NOTHING is "
                          "interpreted from here on", iv, p);
        break;
      }
      p = end;
      if (!addr) continue;
      s_iaddrs[s_icount] = addr;
      snprintf(s_inames[s_icount], sizeof s_inames[0], "INTERP@0x%08X", addr);
      psxport_recomp()->shard_set_override(addr, interp_hook);
      cfg_logi("ndiff", "%s ARMED — this body runs INTERPRETED from guest RAM instead of as "
                        "recompiled C. Under PSXPORT_NDIFF each call is compared against the "
                        "recompiled body; zero reported calls means it never ran, which is not the "
                        "same answer as 'it matched'.", s_inames[s_icount]);
      s_icount++;
    }
  }

  // PSXPORT_NDIFF_IDENTITY=<hex guest address>[,<hex>...] — off unless asked for. Running any body
  // twice per call is far too expensive for a normal run, and this answers a one-off question per
  // renderer.
  const char* e = cfg_str("PSXPORT_NDIFF_IDENTITY");
  if (!e || !*e) return;
  for (const char* p = e; *p && s_count < kMaxProbes;) {
    while (*p == ',' || *p == ' ') p++;
    if (!*p) break;
    char* end = nullptr;
    const uint32_t addr = (uint32_t)strtoul(p, &end, 16);
    if (end == p) {
      // A silently-skipped token is how a probe reports "never called" for an address it never armed.
      cfg_loge("ndiff", "PSXPORT_NDIFF_IDENTITY=%s: '%s' is not a hex guest address (e.g. 8004F000); "
                        "NOTHING is armed from here on", e, p);
      return;
    }
    p = end;
    if (!addr) continue;
    s_addrs[s_count] = addr;
    snprintf(s_names[s_count], sizeof s_names[0], "IDENTITY@0x%08X", addr);
    psxport_recomp()->shard_set_override(addr, ident_hook);
    s_count++;
  }
  if (!s_count) {
    cfg_loge("ndiff", "PSXPORT_NDIFF_IDENTITY=%s armed NO addresses", e);
    return;
  }
  for (int i = 0; i < s_count; i++)
    cfg_logi("ndiff", "%s ARMED — running the generated body against itself. A divergence means the "
                      "differential CANNOT validate a function of this shape, and owning it would need "
                      "a different acceptance test. Zero calls means it never ran in this capture, "
                      "which is a different answer from 'it diverged'.", s_names[i]);
}
