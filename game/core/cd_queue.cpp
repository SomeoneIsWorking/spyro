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
#include "recomp_iface.h"
#include "rec_decls.h"     // generated: gen_func_8002BBE0 — the body we super-call
#include <lucent/log.h>
#include "overlay_router.h"  // overlay_note_load — give the arena slot a load-time identity
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
  // The ONE legitimate guard: this captures five guest words that must be sampled BEFORE the
  // super-call runs, which is work the logger cannot defer for us. It guards the CAPTURE, not the
  // print — the print below is unconditional and gates itself.
  uint32_t st = 0, pend = 0, q = 0, arg = 0, gate = 0;
  if (lucent::channel_on("cdq")) {
    st = c->mem_r32(kStatus); pend = c->mem_r32(kPending);
    q = c->mem_r32(kQueued);  arg  = c->mem_r32(kReqArg);
    gate = c->mem_r32(kGate);
  }

  gen_func_8002BBE0(c);   // super-call: the recompiled body, unmodified

  // Log BEFORE and AFTER together: the interesting question is which word the service routine
  // actually moved on this pass, and a one-sided snapshot cannot answer that.
  lucent::debug("cdq", "service: gate={} status=0x{:X}->0x{:X} pending={}->{} queued={}->{} arg=0x{:08X}",
                gate, st, c->mem_r32(kStatus), pend, c->mem_r32(kPending),
                q, c->mem_r32(kQueued), arg);
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
  lucent::debug("cdq", "delivered CD completion -> gate now {}", c->mem_r32(kGate));
}

void cd_retry_step(Core* c) {
  static unsigned n = 0;
  // `n < 8` is a RATE LIMIT on a per-retry site, not a channel gate — the channel gate is inside
  // lucent::debug. Dropping it would turn one line into hundreds of thousands.
  if (n < 8)
    lucent::debug("cdq", "retry#{} ENTER gate=[0x80076BB8]={} a={} b={} status=0x{:X}",
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

  if (n <= 8)
    lucent::debug("cdq", "retry#{} EXIT  status=0x{:X} pending={} queued={}",
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
    if (hits < 4)                                                                          \
      lucent::debug("cdq", "probe " #NAME " (0x{:08X}) a0=0x{:08X} a1=0x{:08X} a2=0x{:08X}" \
                           " a3=0x{:08X}",                                                 \
                    (unsigned)ADDR, c->r[4], c->r[5], c->r[6], c->r[7]);                   \
    hits++;                                                                                \
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
  lucent::debug("cdq", "read issued: dest=0x{:08X} mode=0x{:X} lba={}", dest, c->r[6], lba);
  if (lba >= 0) { cd_stream_lba = lba; cd_stream_dest = dest; }
  cd_completion_pending = true;   // this read is complete the moment it is issued (synchronous CD)
  gen_func_80065DBC(c);
}


// ── THE GAME-LEVEL LOADER ────────────────────────────────────────────────────────────────────────
// func_80016500(a0 = LBA, a1 = destination, a2 = length in bytes).
//
// Confirmed by observation, not inference: probes logged a0=0x25 (37 — exactly WAD.WAD's LBA from
// the disc directory) with a1=0x8007AA38 / a2=0x800 (one sector), then a1=0x801BF800 / a2=0x40000
// (256 KB) — a second, larger load to a different buffer. That is the (offset, destination, length)
// contract the reference consumer owns via cdFileLoad, and it is the right layer: the destination is
// an ARGUMENT here, rather than something to reverse-engineer out of a transfer path that our own
// lower-level override prevented from ever running (C018).
//
// Serve the bytes BEFORE the body runs, then super-call. The guest's own logic — its wait loop, its
// bookkeeping, its return value — is left entirely intact; we only ensure the data it is about to
// wait for is already there. That keeps the recompiled body live and diffable rather than replacing
// behaviour we have not fully RE'd.
void cd_loader(Core* c) {
  // NOTE: this function takes a FIFTH argument on the stack. Its prologue does `sp -= 56` and then
  // reads sp+72, i.e. caller_sp+16 — so at override entry (before the body's prologue runs) it is at
  // r[29]+16. a0 is CONSTANT at 37 across every call while lengths and destinations vary, so a0 is
  // not a per-call offset; the stack argument is the prime candidate for the real one.
  const uint32_t lba = c->r[4], dest = c->r[5], len = c->r[6];
  const uint32_t arg_off = c->r[7];            // a3 = byte offset into the archive
  const uint32_t arg5 = c->mem_r32(c->r[29] + 16);
  // a3 is the BYTE OFFSET into the archive, and a0 is its base LBA (constant 37 = WAD.WAD).
  // Observed a3 values are always 2048-aligned — 0x00000, 0x5F000, 0x5F800, 0x5B800, 0x00800 — i.e.
  // whole sectors, which is what makes the mapping unambiguous:
  //     sector = a0 + a3 / 2048
  // Reading from a0 alone (ignoring a3) fetched the archive's first sectors for every request: the
  // right destination and length, but the wrong CONTENT. Bytes moved, so it looked like it worked.
  const uint32_t start = lba + (arg_off / 2048u);
  uint32_t moved = 0;
  if (len && dest) {
    uint8_t sec[2048];
    for (uint32_t off = 0; off < len; off += sizeof sec) {
      if (!disc_read_sector(&c->game->disc, start + off / sizeof sec, sec)) break;
      const uint32_t n = (len - off) < sizeof sec ? (len - off) : (uint32_t)sizeof sec;
      for (uint32_t i = 0; i < n; i++) c->mem_w8(dest + off + i, sec[i]);
      moved += n;
    }
  }
  // C024's falsifier: the guest calls 0x8007ABAC = heapBase+0x174, inside what this loader places.
  // If that region really is CODE loaded from WAD.WAD, those words decode as MIPS; if it is data
  // reached through a corrupt pointer, they will not. Log the raw words and decode them offline —
  // cheaper than embedding a disassembler here, and the words are the evidence either way.
  if (moved > 0x174 + 16)
    lucent::debug("cdq", "  callsite-words @0x{:08X}: {:08X} {:08X} {:08X} {:08X}",
                  dest + 0x174, c->mem_r32(dest + 0x174), c->mem_r32(dest + 0x178),
                  c->mem_r32(dest + 0x17C), c->mem_r32(dest + 0x180));
  lucent::debug("cdq", "loader: a0={} dest=0x{:08X} len={} a3=0x{:08X} arg5=0x{:08X} -> moved {} bytes",
                lba, dest, len, arg_off, arg5, moved);
  // Give the overlay router its LOAD-TIME identity for the arena slot. This must happen HERE, right
  // after the copy and before the guest's own bookkeeping runs: overlay_router.cpp is explicit that an
  // image's signature matches the RAM at its base only until the game mutates the image's header
  // pointer table. Routing then goes by identity rather than a re-scan, and stays correct once more
  // than one overlay shares the arena (claim C032, docs/issues/0013). A non-overlay blob — the bulk
  // data loads to 0x801BF800 — is not at a slot base, so this is a no-op for them.
  if (moved) overlay_note_load(c, dest);
  gen_func_80016500(c);   // super-call: the guest's own wait/bookkeeping, now with data present
}

// ── the ASYNC/streaming read primitive 0x80016698 ───────────────────────────────────────────────
// THE SECOND READ PATH, and the one that actually loads levels. 0x80016500 (11 static call sites) is
// only the SYNC boot loader; 0x80016698 has NINETEEN call sites spanning 0x80014608-0x80015BC0 and is
// what the streaming machine 0x80015370 drives. Missing it is why "6 loader calls, none a level" read
// as "the game never asks for the level" when in fact it asks ~10 times through here.
//
// Same argument contract, confirmed from the body rather than assumed — its prologue moves
// a0->s2, a1->s4, a2->s3, a3->s1 and reads the 5th argument from sp+0x40, then at 0x800166F0 computes
// `a0 = s2 + (s1 >> 11)`, i.e. sector = baseLBA + byteOffset/2048, exactly as 0x80016500 does. At
// override entry (before the prologue) the 5th argument is at r[29]+0x10.
//
// WHY SERVING IT HERE MAKES THE EXISTING COMPLETION HONEST. This port already delivers the guest's CD
// callback for every issued read (deliver_cd_complete). For reads that went through 0x80016500 that is
// truthful — the bytes are there. For reads through THIS function nothing moved, so the guest was told
// "your read finished" over an untouched buffer: it sailed through all six streaming phases with zero
// bytes landed, installed the level handler 0x8008772C from the per-level table, and called into memory
// no load had ever written. Serving the bytes at issue time is not a new mechanism — our disc is
// synchronous, so the data IS available the moment the read is issued, and delivering completion after
// it is then a true statement rather than a lie.
void cd_stream_read(Core* c) {
  const uint32_t lba = c->r[4], dest = c->r[5], len = c->r[6];
  const uint32_t arg_off = c->r[7];
  const uint32_t arg5 = c->mem_r32(c->r[29] + 16);
  const uint32_t start = lba + (arg_off / 2048u);
  uint32_t moved = 0;
  if (len && dest) {
    uint8_t sec[2048];
    for (uint32_t off = 0; off < len; off += sizeof sec) {
      if (!disc_read_sector(&c->game->disc, start + off / sizeof sec, sec)) break;
      const uint32_t n = (len - off) < sizeof sec ? (len - off) : (uint32_t)sizeof sec;
      for (uint32_t i = 0; i < n; i++) c->mem_w8(dest + off + i, sec[i]);
      moved += n;
    }
  }
  lucent::debug("cdq", "stream: a0={} dest=0x{:08X} len={} a3=0x{:08X} arg5=0x{:08X} -> moved {} bytes",
                lba, dest, len, arg_off, arg5, moved);
  // Same load-time identity the sync loader records, for the same reason: an overlay's signature only
  // matches its slot before the game mutates the image header.
  if (moved) overlay_note_load(c, dest);
  gen_func_80016698(c);   // super-call: Setmode/Setloc/in-flight flag bookkeeping, now over real data
}

// ── loader-hunt probes ───────────────────────────────────────────────────────────────────────────
// Walking UP the call chain from the read issue to find the GAME-level loader — the first function
// whose arguments look like (destination, offset, length). That is the layer the reference consumer
// owns (cdFileLoad / cdAsyncRead), and owning it sidesteps the transfer path entirely because the
// destination arrives as an argument. Chain observed from backtraces:
//   main 0x80012204 -> 0x800127C0 -> 0x8001250C -> 0x80016500 -> (read issue)
// 0x80012480 also builds the CD callback address, so it is part of the CD setup layer.
#define LOADER_PROBE(NAME, ADDR)                                                          \
  void lp_##NAME(Core* c) {                                                                \
    static unsigned h = 0;                                                                 \
    if (h < 3)                                                                              \
      lucent::debug("cdq", "LOADER? " #NAME " a0={:08X} a1={:08X} a2={:08X} a3={:08X}"      \
                           " sp={:08X}",                                                    \
                    c->r[4], c->r[5], c->r[6], c->r[7], c->r[29]);                          \
    h++;                                                                                    \
    gen_func_##NAME(c);                                                                     \
  }
LOADER_PROBE(8001250C, 0x8001250Cu)
LOADER_PROBE(80012480, 0x80012480u)
LOADER_PROBE(800127C0, 0x800127C0u)
#undef LOADER_PROBE


// ── post-CD stall probe (issue 0012) ─────────────────────────────────────────────────────────────
// func_8005CBB0 is where the boot stalls now that the CD path is served. It polls two globals
// against 1 and calls func_8005DB84; it also WRITES poll-B itself, so it is a state machine rather
// than a pure predicate. Log what it actually sees — the CD chain taught that static decode 
// mis-identifies these (four wrong conclusions), while logging real words settles them in one run.
constexpr uint32_t kPollA = 0x800730F0u;
constexpr uint32_t kPollB = 0x80073588u;
constexpr uint32_t kPollArg = 0x800730E8u;

void probe_8005CBB0(Core* c) {
  static unsigned h = 0;
  const uint32_t a0 = c->r[4];
  if (h < 6)
    lucent::debug("cdq", "stall#{} ENTER a0={} A[0x800730F0]={} B[0x80073588]={} arg[0x800730E8]=0x{:08X}",
                  h, a0, c->mem_r32(kPollA), c->mem_r32(kPollB), c->mem_r32(kPollArg));
  h++;
  gen_func_8005CBB0(c);
  if (h <= 6)
    lucent::debug("cdq", "stall#{} EXIT  v0={} A={} B={}", h - 1, c->r[2],
                  c->mem_r32(kPollA), c->mem_r32(kPollB));
}


// Who OPENED the event whose handle (0xF1000000-based) func_8005CBB0 polls? func_8005BB78 is the
// sole writer of that handle global, so it is the OpenEvent site. Its arguments name the event:
// PSX OpenEvent(class, spec, mode, handler). Log them, plus what the poll primitive is handed.
void probe_8005BB78(Core* c) {
  lucent::debug("cdq", "EVENT-OPEN site a0=0x{:08X} a1=0x{:08X} a2=0x{:08X} a3=0x{:08X}",
                c->r[4], c->r[5], c->r[6], c->r[7]);
  gen_func_8005BB78(c);
  lucent::debug("cdq", "EVENT-OPEN -> handle=0x{:08X} stored=0x{:08X}", c->r[2], c->mem_r32(0x800730E8u));
}

// The poll primitive func_8005CBB0 calls with the handle — TestEvent-shaped.
void probe_8005DB84(Core* c) {
  static unsigned h = 0;
  if (h < 4) lucent::debug("cdq", "EVENT-TEST a0=0x{:08X}", c->r[4]);
  h++;
  gen_func_8005DB84(c);
  if (h <= 4) lucent::debug("cdq", "EVENT-TEST -> v0={}", c->r[2]);
}

}  // namespace

void spyro_register_cd_queue() {
  psxport_recomp()->shard_set_override(0x8005BB78u, probe_8005BB78);
  psxport_recomp()->shard_set_override(0x8005DB84u, probe_8005DB84);
  psxport_recomp()->shard_set_override(0x8005CBB0u, probe_8005CBB0);
  psxport_recomp()->shard_set_override(0x80016500u, cd_loader);
  psxport_recomp()->shard_set_override(0x80016698u, cd_stream_read);
  psxport_recomp()->shard_set_override(0x8001250Cu, lp_8001250C);
  psxport_recomp()->shard_set_override(0x80012480u, lp_80012480);
  psxport_recomp()->shard_set_override(0x800127C0u, lp_800127C0);
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
