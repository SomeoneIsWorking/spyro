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
#include "cfg.h"      // cfg_str — the PSXPORT_*_FN address lists are feature flags, not diagnostics
#include "spyro_game.h"
#include <lucent/log.h>
#include <cstdio>
#include <cstdlib>
#include <array>

void interp_call(Core* c, uint32_t pc);   // interp.cpp — nested call that leaves the guest's ra alone

namespace {

// RasterizeSpritePrimQueue's INPUT census. This deliberately reads the game's request queue and mesh
// records before redispatching the untouched guest body. It does not inspect the OT or GPU packets:
// those are renderer output and cannot define a native producer. The fixed addresses and capacities
// are symbols/sizes in Spyro's executable (open-spyro symbols.csv); the record fields and primitive
// stream layout are reads performed by the body at 0x80022A2C itself.
constexpr uint32_t kSpriteRenderer = 0x80022A2Cu;
constexpr uint32_t kSpriteQueue = 0x800720F4u;
constexpr uint32_t kActorMeshTable = 0x80076378u;
constexpr uint32_t kRamBegin = 0x80000000u;
constexpr uint32_t kRamEnd = 0x80200000u;
constexpr uint32_t kQueueCapacity = 256u;

struct SpriteQueueCensus {
  bool armed = false;
  uint64_t calls = 0;
  uint64_t empty_calls = 0;
  uint64_t records = 0;
  uint64_t primitives = 0;
  uint64_t bit11 = 0;
  uint64_t bit01 = 0;
  uint64_t gouraud_quad = 0;
  uint64_t gouraud_tri = 0;
  uint64_t invalid_actor = 0;
  uint64_t absent_mesh = 0;
  uint64_t absent_stream = 0;
  uint64_t sentinel_mesh = 0;
  uint64_t invalid_mesh = 0;
  uint64_t invalid_stream = 0;
  uint64_t invalid_index = 0;
  uint64_t unterminated_queues = 0;
  std::array<bool, 65536> mesh_seen{};
  std::array<bool, 65536> invalid_stream_seen{};
  uint32_t distinct_meshes = 0;
} s_spriteq;

bool ram_range(uint32_t addr, uint32_t bytes) {
  // Asset relocation intentionally leaves some pointers in the physical-RAM alias (the renderer
  // itself masks bit 31 from its vertex pointer). Core::mem_r* accepts both aliases, so rejecting
  // low pointers here would call every valid streamed mesh corrupt.
  const uint32_t physical = addr & 0x1FFFFFFFu;
  return (addr < 0x00200000u || (addr >= kRamBegin && addr < kRamEnd)) &&
         physical < 0x00200000u && bytes <= 0x00200000u - physical;
}

void census_sprite_queue(Core* c) {
  s_spriteq.calls++;
  uint32_t call_records = 0;
  bool terminated = false;
  for (uint32_t qi = 0; qi < kQueueCapacity; ++qi) {
    const uint32_t actor = c->mem_r32(kSpriteQueue + qi * 4u);
    if (!actor) { terminated = true; break; }
    if (!ram_range(actor, 0x58u)) { s_spriteq.invalid_actor++; continue; }
    call_records++;
    s_spriteq.records++;

    const uint16_t mesh_index = c->mem_r16(actor + 0x36u);
    if (!s_spriteq.mesh_seen[mesh_index]) {
      s_spriteq.mesh_seen[mesh_index] = true;
      s_spriteq.distinct_meshes++;
    }
    const uint32_t mesh = c->mem_r32(kActorMeshTable + (uint32_t)mesh_index * 4u);
    // The queue is built before visibility rejection. A null mesh entry is therefore an observed
    // input state, not corruption: the guest only dereferences it inside its visible branch.
    if (!mesh) { s_spriteq.absent_mesh++; continue; }
    if (!ram_range(mesh, 0x10u)) { s_spriteq.invalid_mesh++; continue; }
    const uint32_t vertex_count = c->mem_r8(mesh + 0u);
    const uint32_t primitive_count = c->mem_r8(mesh + 1u);
    const uint32_t stream = c->mem_r32(mesh + 0x0Cu);
    // Slot 0 is the executable's explicit all-ones sentinel descriptor. These records are rejected
    // by the visibility branch before the body reaches mesh decoding; keep them in the denominator
    // without reporting their deliberately-invalid stream pointer as corruption.
    if (vertex_count == 0xFFu && primitive_count == 0xFFu && stream == 0xFFFFFFFFu) {
      s_spriteq.sentinel_mesh++;
      continue;
    }
    if (!stream && primitive_count) { s_spriteq.absent_stream++; continue; }
    if (!ram_range(stream, primitive_count * 8u)) {
      s_spriteq.invalid_stream++;
      if (!s_spriteq.invalid_stream_seen[mesh_index]) {
        s_spriteq.invalid_stream_seen[mesh_index] = true;
        lucent::info("spriteq", "unavailable stream state: mesh_index={} mesh=0x{:08X} "
                                "vertex_count={} primitive_count={} stream=0x{:08X}; queued records "
                                "are inspected before the guest's visibility branch",
                     mesh_index, mesh, vertex_count, primitive_count, stream);
      }
      continue;
    }

    for (uint32_t pi = 0; pi < primitive_count; ++pi) {
      const uint32_t packed = c->mem_r32(stream + pi * 8u);
      const uint32_t i0 = (packed >> 21u) & 0x1FCu;
      const uint32_t i1 = (packed >> 14u) & 0x1FCu;
      const uint32_t i2 = (packed >> 7u) & 0x1FCu;
      const uint32_t i3 = packed & 0x1FCu;
      if (i0 / 4u >= vertex_count || i1 / 4u >= vertex_count ||
          i2 / 4u >= vertex_count || i3 / 4u >= vertex_count) {
        s_spriteq.invalid_index++;
        continue;
      }
      s_spriteq.primitives++;
      if ((packed & 3u) == 3u) s_spriteq.bit11++;
      else if ((packed & 1u) != 0u) s_spriteq.bit01++;
      else if (i2 != i3) s_spriteq.gouraud_quad++;
      else s_spriteq.gouraud_tri++;
    }
  }
  if (!terminated) s_spriteq.unterminated_queues++;
  if (!call_records) s_spriteq.empty_calls++;
}

void sprite_queue_hook(Core* c) {
  census_sprite_queue(c);
  const RecompRegistry* R = psxport_recomp();
  R->shard_set_override(kSpriteRenderer, nullptr);
  R->main_dispatch(c, kSpriteRenderer);
  R->shard_set_override(kSpriteRenderer, sprite_queue_hook);
}

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
  if (cfg_str("PSXPORT_SPRITE_QUEUE_CENSUS")) {
    s_spriteq.armed = true;
    psxport_recomp()->shard_set_override(kSpriteRenderer, sprite_queue_hook);
    lucent::info("spriteq", "ARMED input census at 0x{:08X}: queue capacity {}, scanning game actor + "
                            "mesh records before the unchanged guest renderer. The run-end report "
                            "prints calls and records even when both are zero.",
                 kSpriteRenderer, kQueueCapacity);
  }
  // PSXPORT_MUTE_FN=<hex guest address>[,<hex>...] — replace these bodies with nothing.
  if (const char* m = cfg_str("PSXPORT_MUTE_FN")) {
    for (const char* p = m; *p;) {
      while (*p == ',' || *p == ' ') p++;
      if (!*p) break;
      char* end = nullptr;
      const uint32_t addr = (uint32_t)strtoul(p, &end, 16);
      if (end == p) {
        // `m` is non-null (checked above) and `p` points into it, so neither can be a null
        // `const char*` — the one std::format case printf would have survived and this would not.
        lucent::error("ndiff", "PSXPORT_MUTE_FN={}: '{}' is not a hex guest address; NOTHING is muted "
                               "from here on", m, p);
        break;
      }
      p = end;
      if (!addr) continue;
      psxport_recomp()->shard_set_override(addr, mute_hook);
      lucent::info("ndiff", "MUTE@0x{:08X} — this body is REPLACED BY NOTHING. Whatever disappears "
                            "from the frame is exactly its visual contribution. The run is "
                            "diagnostic: guest state this body would have written is simply absent.",
                   addr);
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
        lucent::error("ndiff", "PSXPORT_INTERP_FN={}: '{}' is not a hex guest address; NOTHING is "
                               "interpreted from here on", iv, p);
        break;
      }
      p = end;
      if (!addr) continue;
      s_iaddrs[s_icount] = addr;
      snprintf(s_inames[s_icount], sizeof s_inames[0], "INTERP@0x%08X", addr);
      psxport_recomp()->shard_set_override(addr, interp_hook);
      lucent::info("ndiff", "{} ARMED — this body runs INTERPRETED from guest RAM instead of as "
                            "recompiled C. Under PSXPORT_NDIFF each call is compared against the "
                            "recompiled body; zero reported calls means it never ran, which is not "
                            "the same answer as 'it matched'.", s_inames[s_icount]);
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
      lucent::error("ndiff", "PSXPORT_NDIFF_IDENTITY={}: '{}' is not a hex guest address (e.g. "
                             "8004F000); NOTHING is armed from here on", e, p);
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
    lucent::error("ndiff", "PSXPORT_NDIFF_IDENTITY={} armed NO addresses", e);
    return;
  }
  for (int i = 0; i < s_count; i++)
    lucent::info("ndiff", "{} ARMED — running the generated body against itself. A divergence means "
                          "the differential CANNOT validate a function of this shape, and owning it "
                          "would need a different acceptance test. Zero calls means it never ran in "
                          "this capture, which is a different answer from 'it diverged'.", s_names[i]);
}

void spyro_sprite_queue_census_finish() {
  if (!s_spriteq.armed) return;
  lucent::info("spriteq", "CENSUS: calls={} empty_calls={} records={} distinct_meshes={} primitives={} "
                          "variants(bit11={}, bit01={}, gouraud_quad={}, gouraud_tri={}) absent(mesh={}, "
                          "stream={}) sentinel_mesh={} "
                          "invalid(actor={}, mesh={}, stream={}, vertex_index={}) unterminated_queues={}",
               s_spriteq.calls, s_spriteq.empty_calls, s_spriteq.records, s_spriteq.distinct_meshes,
               s_spriteq.primitives, s_spriteq.bit11, s_spriteq.bit01, s_spriteq.gouraud_quad,
               s_spriteq.gouraud_tri, s_spriteq.absent_mesh, s_spriteq.absent_stream,
               s_spriteq.sentinel_mesh,
               s_spriteq.invalid_actor,
               s_spriteq.invalid_mesh, s_spriteq.invalid_stream, s_spriteq.invalid_index,
               s_spriteq.unterminated_queues);
}
