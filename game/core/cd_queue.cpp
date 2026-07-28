// cd_queue.cpp — Spyro's CD request queue: observation first, ownership next.
//
// Spyro layers its own asynchronous CD request queue on top of libcd (decoded in
// docs/issues/0006). The boot's read-wait (func_80016500) spins until the CD status word has
// bit 0x40, and the producer of that bit is func_8002BBE0 — the queue's service routine.
//
// WHY THIS FILE EXISTS AS AN OVERRIDE RATHER THAN A gdb SESSION.
// A gdb breakpoint on gen_func_8002BBE0 failed to hit within 120s even though a stack profile had
// shown that function running (instrument I004), so gdb is not a trustworthy way to answer "what is
// this guest state doing". An override runs INSIDE the port, on the real dispatch path, and can log
// the actual guest words each time the routine is entered.
//
// SUPER-CALL, NOT REPLACEMENT. This handler logs and then calls the recompiled body, so behaviour is
// unchanged and the body stays live and diffable. That is the framework's override design and the
// right first step of owning a function: observe it faithfully before reimplementing any of it.
// Nothing here writes guest memory — in particular it does NOT set the status bit the wait loop
// wants. That bit has a known producer now, and faking it would discard the understanding that
// decoding the queue bought.
#include "core.h"
#include "game.h"
#include "cfg.h"
#include "recomp_iface.h"
#include "rec_decls.h"     // generated: gen_func_8002BBE0 — the body we super-call
#include "spyro_game.h"

namespace {

// The queue's guest state (docs/issues/0006).
constexpr uint32_t kStatus  = 0x800774B4u;   // CD status word; the wait loop tests bit 0x40
constexpr uint32_t kPending = 0x800776C4u;   // pending-event code (service completes it at 8|9)
constexpr uint32_t kQueued  = 0x800776C8u;   // queued-request slot (drained -> func_800567F4)
constexpr uint32_t kReqArg  = 0x800776B0u;   // request argument handed to the processor
constexpr uint32_t kGate    = 0x80076BB8u;   // wait-loop gate: must be 0 to even test the rest

// Service routine 0x8002BBE0. Log the queue words on entry, then run the real body.
void cd_service(Core* c) {
  const bool on = cfg_dbg("cdq");
  uint32_t st = 0, pend = 0, q = 0, arg = 0, gate = 0;
  if (on) {
    st = c->mem_r32(kStatus); pend = c->mem_r32(kPending);
    q = c->mem_r32(kQueued);  arg  = c->mem_r32(kReqArg);
    gate = c->mem_r32(kGate);
  }

  gen_func_8002BBE0(c);   // super-call: the recompiled body, unmodified

  if (on) {
    // Log BEFORE and AFTER together: the interesting question is which word the service routine
    // actually moved on this pass, and a one-sided snapshot cannot answer that.
    cfg_logf("cdq", "service: gate=%u status=0x%X->0x%X pending=%u->%u queued=%u->%u arg=0x%08X",
             gate, st, c->mem_r32(kStatus), pend, c->mem_r32(kPending),
             q, c->mem_r32(kQueued), arg);
  }
}

// The retry body's FIRST call, 0x800163E4. A 5-sample profile puts the spin here, and it never
// returns — which is why the service routine above (later in the same retry body) is never reached.
// Log entry and exit so "entered once and hung" is distinguishable from "entered repeatedly", and
// dump the two globals its early-out tests: it returns immediately unless
// [0x800758E0] != 0 && [0x800758E0] < [0x800758CC].
constexpr uint32_t kA = 0x800758E0u;
constexpr uint32_t kB = 0x800758CCu;

void cd_retry_step(Core* c) {
  static unsigned n = 0;
  const bool on = cfg_dbg("cdq");
  if (on && n < 8)
    cfg_logf("cdq", "retry#%u ENTER gate=[0x80076BB8]=%u a=%d b=%d status=0x%X",
             n, c->mem_r32(kGate), (int)c->mem_r32(kA), (int)c->mem_r32(kB), c->mem_r32(kStatus));
  n++;

  gen_func_800163E4(c);   // super-call

  if (on && n <= 8)
    cfg_logf("cdq", "retry#%u EXIT  status=0x%X pending=%u queued=%u",
             n - 1, c->mem_r32(kStatus), c->mem_r32(kPending), c->mem_r32(kQueued));
}

}  // namespace

void spyro_register_cd_queue() {
  psxport_recomp()->shard_set_override(0x800163E4u, cd_retry_step);
  // Game-code overrides go through the RECOMP override registry, not PlatformHle: that table is for
  // I/O and BIOS-library primitives and validates against GameConfig::hle's windows, which
  // deliberately exclude game logic. Game functions are owned top-down through here instead.
  psxport_recomp()->shard_set_override(0x8002BBE0u, cd_service);
}
