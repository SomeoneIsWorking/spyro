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

// The guest's CD event callback, 0x80016490. It has NO direct callers — its address is only ever
// BUILT and handed to libcd's callback registration (func_8006623C), which is the signature of an
// interrupt handler. Called with a0 = event type; a0 == 2 is completion, and that path clears the
// gate at 0x80076BB8 plus its two companion globals.
//
// On hardware libcd invokes it from the CD interrupt. This runtime raises no guest interrupts, so it
// is never called and the gate is never cleared — which is the whole of the post-splash stall.
//
// Delivering it is REPRODUCING THE HARDWARE EVENT, not faking a value: our CD is synchronous, so a
// read is already complete by the time the guest could have been interrupted, and completion is
// exactly the event the controller would have raised. The alternative — writing 0 to the gate
// ourselves — would be the poke, because it would skip everything else the callback does.
// Streaming-read state. Armed when the guest issues its read (the probe below sees the destination
// buffer in a1) and advanced one sector per delivered completion, mirroring a ReadN stream.
// -1 = no read in flight.
int32_t  cd_stream_lba  = -1;
uint32_t cd_stream_dest = 0;

// One completion per issued read. Re-armed at the READ ISSUE point, not by observing the gate reach
// 0: the guest re-issues its read (re-setting the gate) before the retry body samples it, so a
// gate-watching trigger latches after the first delivery and never fires again.
bool cd_completion_pending = false;

constexpr uint32_t kCdCallback = 0x80016490u;
constexpr uint32_t kEventComplete = 2u;

void deliver_cd_complete(Core* c) {
  const uint32_t saved_a0 = c->r[4], saved_ra = c->r[31];
  c->r[4] = kEventComplete;
  gen_func_80016490(c);
  c->r[4] = saved_a0; c->r[31] = saved_ra;
  if (cfg_dbg("cdq"))
    cfg_logf("cdq", "delivered CD completion -> gate now %u", c->mem_r32(kGate));
}

void cd_retry_step(Core* c) {
  static unsigned n = 0;
  const bool on = cfg_dbg("cdq");
  if (on && n < 8)
    cfg_logf("cdq", "retry#%u ENTER gate=[0x80076BB8]=%u a=%d b=%d status=0x%X",
             n, c->mem_r32(kGate), (int)c->mem_r32(kA), (int)c->mem_r32(kB), c->mem_r32(kStatus));
  n++;

  gen_func_800163E4(c);   // super-call

  // Our CD is synchronous: by the time the guest reaches its polling loop, any issued read has
  // already completed. This retry body IS that polling point — where the CD interrupt would have
  // been noticed on hardware — so deliver the completion event here.
  //
  // EDGE-triggered on the in-flight gate, not level-triggered: exactly one completion per request,
  // which is what the controller raises. A level trigger would re-deliver every iteration and call
  // the guest's handler repeatedly for a single read.
  // NO SECTOR TRANSFER HERE — an earlier attempt is reverted, see below.
  // WHY NO TRANSFER: a hypothesis was tested and NOT supported. Probes showed both read-path
  // functions receiving a1 = 0x8007AA38 (heapBase), so a1 looked like the destination buffer, and a
  // per-sector copy from Cd::setloc_lba into it was tried. Predicted effect: the guest advances past
  // its first sector. Observed: it re-issued the SAME read at LBA 37 and nothing advanced — frames
  // stayed at 8. An inferred destination that does not produce its predicted behaviour is an
  // unvalidated 2048-byte-per-iteration write into guest memory, so it was removed rather than left
  // in looking like progress. a1 may be a descriptor or a mode block rather than a buffer.
  //
  // Also learned: the edge-trigger below LATCHES. The guest re-issues its read (re-setting the gate)
  // before the next sample, so `delivered` never re-arms and exactly one completion is ever sent.
  // Whatever replaces this must key off the read ISSUE, not off observing the gate at 0.
  //
  // INCOMPLETE (unchanged): completion is delivered without data —
  //
  // This advances the state machine correctly (gate clears, the wait in func_80016500 succeeds, the
  // guest issues its next request), which is what proved the gate/callback analysis right. But it is
  // only half of a read: the guest is told "your read finished" when nothing landed in its buffer.
  // Observable consequence, and the reason this is not mistaken for working: the guest re-seeks the
  // SAME sector every time — PSXPORT_DEBUG=cd shows only "LBA 37", never an advancing position — so
  // it is retrying, not loading.
  //
  // THE REAL FIX couples the two: transfer the sectors from Cd::setloc_lba into the guest's
  // destination buffer, and deliver completion only once that has happened. That needs the transfer
  // path (func_8006606C and friends) read out of its body first — see docs/issues/0003. Until then
  // this stays deliberately visible rather than dressed up as a working read.
  if (cd_completion_pending) { cd_completion_pending = false; deliver_cd_complete(c); }

  if (on && n <= 8)
    cfg_logf("cdq", "retry#%u EXIT  status=0x%X pending=%u queued=%u",
             n - 1, c->mem_r32(kStatus), c->mem_r32(kPending), c->mem_r32(kQueued));
}

// ── transfer-path probes ─────────────────────────────────────────────────────────────────────────
// Which function actually moves sector bytes, and with what arguments, is the one thing still
// unknown. Static decode has been wrong three times on this stall (issues 0005/0007), so these are
// probes, not conclusions: each logs its arguments once and super-calls the real body. Whichever
// fires — and with what — decides where the native read goes.
#define CD_PROBE(NAME, ADDR)                                                             \
  void probe_##NAME(Core* c) {                                                           \
    static unsigned hits = 0;                                                            \
    if (cfg_dbg("cdq") && hits < 4)                                                       \
      cfg_logf("cdq", "probe " #NAME " (0x%08X) a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X", \
               (unsigned)ADDR, c->r[4], c->r[5], c->r[6], c->r[7]);                       \
    hits++;                                                                               \
    gen_func_##NAME(c);                                                                   \
  }
CD_PROBE(8006606C, 0x8006606Cu)   // sector-size selection (512 vs 585/582 words) — DMA setup shape
CD_PROBE(800659F0, 0x800659F0u)   // "CdRead: sector error" carrier; keeps a1
CD_PROBE(800567F4, 0x800567F4u)   // the queue's request processor
#undef CD_PROBE

// The read issue point. It receives the destination buffer in a1 (observed: 0x8007AA38 = heapBase)
// and the mode in a2, so this is where the stream is armed: start at the position the guest last
// Setloc'd (tracked framework-side in Cd::setloc_lba) and hand sectors over one per completion.
void probe_80065DBC(Core* c) {
  const uint32_t dest = c->r[5];
  const int32_t  lba  = c->game->cd.setloc_lba;
  if (cfg_dbg("cdq"))
    cfg_logf("cdq", "read issued: dest=0x%08X mode=0x%X lba=%d", dest, c->r[6], lba);
  if (lba >= 0) { cd_stream_lba = lba; cd_stream_dest = dest; }
  cd_completion_pending = true;   // this read is complete the moment it is issued (synchronous CD)
  gen_func_80065DBC(c);
}

}  // namespace

void spyro_register_cd_queue() {
  psxport_recomp()->shard_set_override(0x8006606Cu, probe_8006606C);
  psxport_recomp()->shard_set_override(0x800659F0u, probe_800659F0);
  psxport_recomp()->shard_set_override(0x80065DBCu, probe_80065DBC);
  psxport_recomp()->shard_set_override(0x800567F4u, probe_800567F4);
  psxport_recomp()->shard_set_override(0x800163E4u, cd_retry_step);
  // Game-code overrides go through the RECOMP override registry, not PlatformHle: that table is for
  // I/O and BIOS-library primitives and validates against GameConfig::hle's windows, which
  // deliberately exclude game logic. Game functions are owned top-down through here instead.
  psxport_recomp()->shard_set_override(0x8002BBE0u, cd_service);
}
