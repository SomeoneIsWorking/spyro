// native_render.cpp — CAN the per-call differential validate a geometry renderer at all?
//
// THE QUESTION, ASKED BEFORE THE EXPENSIVE WORK. Widescreen and 60fps both require owning this
// game's hand-written assembly renderers (re-frontier: render.own-geometry-family), and every owned
// body in this port is admitted only when ndiff proves it byte-identical to the body it replaces. A
// byte-exact reimplementation of a 278-instruction assembly renderer is days of work whose payoff
// is invisible until it is finished — so the first thing to establish is whether the ACCEPTANCE
// TEST even works on a function of this shape. If it cannot, the whole plan needs rethinking, and
// it is far cheaper to learn that now.
//
// THE EXPERIMENT IS AN IDENTITY: hand ndiff the generated body as BOTH the "native" replacement and
// the substrate reference. It runs the body, rewinds RAM + scratchpad + all GPRs + the COP2
// register file, runs the same body again, and compares. A correct harness on a deterministic
// function MUST report a match. Anything else is the harness or the function telling us this
// validation route is closed — for instance a body that reads state the rewind does not restore
// (host GPU state, a timer, an ordering-table pointer living outside guest RAM), which is exactly
// the hazard the re-frontier already records for spin-loop bodies.
//
// Why 0x8004EBA8: it is the one renderer understood at instruction level end to end (two stages,
// 11/11/10-bit packed vertex deltas, a scratchpad vertex cache indexed by pre-scaled byte offsets
// from the face list, POLY_FT3 at stride 0x1C and F3 at 0x14). It also only WRITES packets into
// guest RAM — the DMA to the GPU happens later, from a different call — so running it twice has no
// host-side effect the rewind would fail to undo. A renderer that submitted to the GPU directly
// could not be tested this way.
//
// TEMPORARY, and gated: this is a measurement, not ownership. It installs nothing on a normal run.
#include "cfg.h" // cfg_str — the PSXPORT_*_FN address lists are feature flags, not diagnostics
#include "core.h"
#include "native_diff.h"
#include "producer_run.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro_game.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <lucent/log.h>

void interp_call(Core *c, uint32_t pc); // interp.cpp — nested call that leaves the guest's ra alone
void spyro_trace_reference_sprite_faces(Core *c);
void spyro_trace_reference_sprite_packets(Core *c, uint32_t begin, uint32_t end);

namespace {

// RasterizeSpritePrimQueue's INPUT census. This deliberately reads the game's request queue and
// mesh records before redispatching the untouched guest body. It does not inspect the OT or GPU
// packets: those are renderer output and cannot define a native producer. The fixed addresses and
// capacities are symbols/sizes in Spyro's executable (open-spyro symbols.csv); the record fields
// and primitive stream layout are reads performed by the body at 0x80022A2C itself.
constexpr uint32_t kSpriteRenderer = 0x80022A2Cu;
constexpr uint32_t kWorldRenderer = 0x800258F0u; // RenderWorldChunks — the GROUND and the cliffs
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
  uint64_t screen_records = 0;
  uint64_t primitives = 0;
  uint64_t bit11 = 0;
  uint64_t bit01 = 0;
  uint64_t gouraud_quad = 0;
  uint64_t gouraud_tri = 0;
  uint64_t screen_primitives = 0;
  uint64_t screen_bit11 = 0;
  uint64_t screen_bit01 = 0;
  uint64_t screen_gouraud_quad = 0;
  uint64_t screen_gouraud_tri = 0;
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
  std::array<uint64_t, 16 * 4> stage_mode_calls{};
  std::array<uint64_t, 16 * 4> stage_mode_records{};
  std::array<uint64_t, 16 * 4> stage_mode_screen_records{};
  std::array<uint64_t, 16 * 4 * 4> stage_mode_variants{};
  std::array<uint64_t, 65536> screen_mesh_records{};
  std::array<uint64_t, 65536> screen_mesh_primitives{};
  std::array<uint32_t, 65536> screen_mesh_actor_flags{};
  uint32_t distinct_meshes = 0;
} s_spriteq;

bool ram_range(uint32_t addr, uint32_t bytes); // defined below; the world census uses it

// ── RenderWorldChunks 0x800258F0's INPUT census (the frontier step after C198). ──────────────────
// The world renderer (the GROUND and the cliffs — the biggest depth contributor, issue 0038) is
// structurally mapped (C198) but not yet owned. This census measures its actual game-state inputs
// at every live call: how many environment chunks the list holds, how many the distance/frustum
// cull keeps, and how many bytes the pool moves per call. It reads the SAME globals the function
// reads (g_Environment's sector list when a0<0, the occlusion-group table when a0>=0), then
// redispatches the unchanged guest body. Like the spriteq census, it prints a run-end report with
// its own denominator, so a run that never reaches the field reports zeros LOUDLY rather than
// passing silent.
//
// g_Environment = 0x800785A8 (from the decomp's `lui at,0x8007; addiu at,0x85A8`):
//   +0x00 m_SectorPointer — the flat chunk-list base (used when a0<0, i.e. from the render driver)
//   +0x04 m_SectorCount   — entries in that list
//   +0x08 m_OcclusionGroups — per-group list table (used when a0>=0)
//   +0x0C m_OcclusionGroupCount
struct WorldCensus {
  bool armed = false;
  uint64_t calls = 0;
  uint64_t negative_group = 0; // a0<0: the render-driver call, flat count-based list
  uint64_t positive_group = 0; // a0>=0: the occlusion-group call, byte-indexed list
  uint64_t flat_entries = 0;   // total sector-count entries across the flat-list calls
  uint64_t occ_entries = 0;    // total byte-index entries across the occlusion-group calls
  uint64_t pool_bytes = 0;
  uint64_t pool_bytes_zero = 0; // calls that moved nothing
  uint32_t max_flat = 0, max_occ = 0;
  uint32_t last_occlusion_group = 0;
  // ORACLE (the depth half of 2D/3D discrimination, re-frontier render.own-geometry-family):
  // RenderWorldChunks' OWN per-call depth signal. The chunk list it projects is at
  // D_8006FCF4+0x1C00 = 0x800718F4 (null-terminated, filled by the cull phase); each entry is a
  // chunk pointer whose byte 0x14's low byte is the vertex count. The projected depth (SZ3) for the
  // LAST chunk processed survives in the scratchpad SZ3 array at 0x1F8002AC (2-byte stride, one per
  // vertex), written by the projection loop (mfc2 C2_SZ3; sh v1,0(t8); t8+=2). C198's falsifier is
  // "a live census showing SZ in the emitted packet body" — so the pool packets are walked too, to
  // prove the depth is NOT in the packets (SZ3 is scratchpad-only).
  uint64_t chunks = 0;
  uint64_t vertices = 0;    // sum of chunk vertex counts (last-chunk SZ3 range is a sample)
  uint64_t depth_calls = 0; // calls where a real (non-empty) chunk list was found
  uint32_t sz3_min = 0x7FFFFFFFu, sz3_max = 0, sz3_lastmin = 0x7FFFFFFFu, sz3_lastmax = 0;
  uint64_t sz3_total = 0, sz3_samples = 0;
  uint64_t pool_packets = 0, pool_quads = 0, pool_tris = 0, pool_malformed = 0;
  uint64_t pool_unparsed_bytes = 0;
  bool sz3_seen = false;
  // Per-face OT bin histogram, accumulated across every live call (the "per-face OT bins" half of
  // the oracle). Bins 0..0x7FF = depth>>7; bin 0 is nearest. Captured per call so it reflects the
  // freshly-linked world faces rather than the run-end (post-DrawOTag) residual.
  uint64_t ot_bins[0x800] = {};
  uint64_t ot_walks = 0, ot_bins_used = 0, ot_truncated = 0, ot_badbase = 0;
} s_world;

void census_world(Core *c) {
  s_world.calls++;
  const uint32_t group = (uint32_t)(int32_t)c->r[4]; // a0 — the occlusion group
  s_world.last_occlusion_group = group;
  const uint32_t env = 0x800785A8u;
  uint32_t count = 0;
  if ((int32_t)group < 0) {
    s_world.negative_group++;
    const uint32_t list_base = c->mem_r32(env + 0x00u);
    count = c->mem_r32(env + 0x04u); // m_SectorCount — the flat list is length-counted
    s_world.flat_entries += count;
    if (count > s_world.max_flat) {
      s_world.max_flat = count;
    }
    lucent::debug("worldcensus",
                  "call {}: a0={} flat sectors={} at 0x{:08X}",
                  s_world.calls,
                  (int32_t)group,
                  count,
                  list_base);
  } else {
    s_world.positive_group++;
    const uint32_t table = c->mem_r32(env + 0x08u);
    const uint32_t list_base =
        ram_range(table + group * 4u, 4u) ? c->mem_r32(table + group * 4u) : 0u;
    // The occlusion-group list is a byte-indexed list (0xFF terminator), not a length count.
    count = 0;
    uint32_t p = list_base;
    while (ram_range(p, 1u) && c->mem_r8(p) != 0xFFu && count < 65536u) {
      p++;
      count++;
    }
    s_world.occ_entries += count;
    if (count > s_world.max_occ) {
      s_world.max_occ = count;
    }
    lucent::debug("worldcensus",
                  "call {}: a0={} occlusion-group indices={} at 0x{:08X}",
                  s_world.calls,
                  (int32_t)group,
                  count,
                  list_base);
  }
}

void world_oracle_pool(Core *c, uint32_t begin, uint32_t end);
void world_oracle_depth(Core *c);
void world_oracle_ot(Core *c);

void world_hook(Core *c) {
  census_world(c);
  const uint32_t pool_before = c->mem_r32(0x800757B0u);
  const RecompRegistry *R = psxport_recomp();
  R->shard_set_override(kWorldRenderer, nullptr);
  R->main_dispatch(c, kWorldRenderer);
  R->shard_set_override(kWorldRenderer, world_hook);
  const uint32_t pool_after = c->mem_r32(0x800757B0u);
  const uint32_t bytes = pool_after > pool_before ? pool_after - pool_before : 0;
  s_world.pool_bytes += bytes;
  if (bytes == 0) {
    s_world.pool_bytes_zero++;
  }
  world_oracle_pool(c, pool_before, pool_after);
  world_oracle_depth(c);
  world_oracle_ot(c);
  lucent::debug("worldcensus",
                "call {} output: pool 0x{:08X} -> 0x{:08X} (+{} bytes)",
                s_world.calls,
                pool_before,
                pool_after,
                bytes);
}

// The depth ORACLE for RenderWorldChunks. After the real body runs, read the chunk list it built
// (0x800718F4, null-terminated) to count chunks + vertices, then sample the LAST chunk's SZ3 depth
// from the scratchpad array at 0x1F8002AC. The values are positive depths (larger = farther); the
// range tells us whether this call produced a real spread of 3D depth or a flat 2D-like plane —
// exactly the signal 2D/3D discrimination needs. See C198: SZ3 is a scratchpad intermediate only.
void world_oracle_depth(Core *c) {
  constexpr uint32_t kChunkList = 0x800718F4u; // D_8006FCF4 + 0x1C00
  constexpr uint32_t kSz3Array = 0x1F8002ACu;  // scratchpad SZ3, 2-byte stride
  constexpr int kMaxChunks = 256;
  uint64_t call_chunks = 0, call_vertices = 0;
  for (int i = 0; i < kMaxChunks; ++i) {
    const uint32_t chunk = c->mem_r32(kChunkList + (uint32_t)i * 4u);
    if (!chunk) {
      break;
    }
    if (!ram_range(chunk, 0x18u)) {
      continue;
    }
    call_chunks++;
    // The projection loop masks the low 2 bits (flag) off the chunk pointer, then reads the vertex
    // count as the low byte of the word at +0x14 (lw s2,0x14($t9); andi s0,s2,0xFF).
    const uint32_t vcount = c->mem_r32(chunk + 0x14u) & 0xFFu;
    call_vertices += vcount;
  }
  s_world.chunks += call_chunks;
  s_world.vertices += call_vertices;
  if (!call_chunks) {
    return;
  }
  s_world.depth_calls++;
  // The SZ3 array holds only the LAST chunk's vertices (overwritten per chunk); sample it with the
  // last chunk's vertex count. The last non-empty chunk's count is what call_vertices includes
  // last. To bound correctly we re-read the last list entry's count.
  uint32_t last_count = 0;
  uint32_t last_chunk = 0;
  for (int i = 0; i < kMaxChunks; ++i) {
    const uint32_t chunk = c->mem_r32(kChunkList + (uint32_t)i * 4u);
    if (!chunk || !ram_range(chunk, 0x18u)) {
      break;
    }
    last_chunk = chunk;
    last_count = c->mem_r32(chunk + 0x14u) & 0xFFu;
  }
  if (!last_chunk || last_count > 256u) {
    lucent::debug("worldcensus",
                  "oracle depth: no valid last chunk (last_chunk=0x{:08X} last_count={})",
                  last_chunk,
                  last_count);
    return;
  }
  uint32_t lo = 0x7FFFFFFFu, hi = 0;
  uint64_t sum = 0, n = 0;
  for (uint32_t i = 0; i < last_count; ++i) {
    const uint32_t addr = kSz3Array + i * 2u;
    // NOTE: no ram_range() guard here — the SZ3 array lives in the GTE SCRATCHPAD (0x1F800000,
    // 1 KB), which is a distinct memory region from guest RAM. Core::mem_r16 handles the alias
    // (mem.cpp:122); ram_range() would reject it as out of RAM, silently zeroing every sample.
    const uint16_t v = c->mem_r16(addr);
    if (i < 4u) {
      lucent::debug("worldcensus", "oracle depth sample[{}] at 0x{:08X} = 0x{:04X}", i, addr, v);
    }
    if (v == 0u) {
      continue; // a zero-depth vertex was culled; it is not a real depth sample
    }
    if (v < lo) {
      lo = v;
    }
    if (v > hi) {
      hi = v;
    }
    sum += v;
    n++;
    s_world.sz3_seen = true;
  }
  if (!n) {
    return;
  }
  s_world.sz3_min = std::min(s_world.sz3_min, lo);
  s_world.sz3_max = std::max(s_world.sz3_max, hi);
  s_world.sz3_lastmin = lo;
  s_world.sz3_lastmax = hi;
  s_world.sz3_total += sum;
  s_world.sz3_samples += n;
  lucent::debug("worldcensus",
                "oracle depth: chunks={} vertices={} last_chunk_sz3 range {}..{} ({} samples)",
                call_chunks,
                call_vertices,
                lo,
                hi,
                n);
}

// Walk the pool packets THIS CALL emitted (pool_before..pool_after). This is the world renderer's
// own geometry — the exact primitive set it drew. Parsing the GPU linked-list headers also proves
// (the negative half of the oracle) that NO depth word is carried in the packet body: SZ3 is a
// scratchpad intermediate only (C198). Strides: gouraud tri 0x1C, quad 0x24, matching the emit
// loop.
void world_oracle_pool(Core *c, uint32_t begin, uint32_t end) {
  if (end < begin) {
    s_world.pool_malformed++;
    return;
  }
  uint32_t p = begin;
  uint64_t packets = 0, quads = 0, tris = 0, malformed = 0;
  while (p < end && packets < 4096u) {
    if (!ram_range(p, 4u)) {
      malformed++;
      break;
    }
    const uint32_t tag = c->mem_r32(p);
    const uint32_t words = tag >> 24;
    const uint32_t bytes = (words + 1u) * 4u;
    if (!words || bytes > end - p) {
      malformed++;
      break;
    }
    // The world renderer's packets are a CUSTOM hand-packed GTE format, NOT standard libgpu
    // POLY_*: the primitive code is a 0x3C-prefixed GTE code OR'd into the SECOND word (0x4),
    // not the libgpu byte-7 code. So classify by the high byte of word 1. The 0x3C000000 prefix
    // (gouraud quad, DPCS colour) is the world renderer's main geometry packet (r_environment.s
    // 0x80026E64: lui at,0x3C000000; or a3,a3,at).
    const uint32_t w1 = c->mem_r32(p + 4u);
    const uint32_t code = w1 >> 24;
    if (code == 0x3Cu) {
      quads++;
    } else {
      tris++;
    }
    packets++;
    p += bytes;
  }
  s_world.pool_packets += packets;
  s_world.pool_quads += quads;
  s_world.pool_tris += tris;
  s_world.pool_malformed += malformed;
  s_world.pool_unparsed_bytes += end - p;
}

// Walk the world OT and accumulate a per-bin face histogram — the "per-face OT bins" half of the
// oracle. g_WorldOT = 0x80075820 holds the OT base; the OT is 0x800 bins x 8 bytes
// (func_80016784(0x800), buffers.h m_WorldOTStart, memset 0x4000). A face's bin = its average
// SZ >> 7 (r_environment.s 0x80026DBC srl t2,t2,7), so the bin index IS the quantized depth bucket:
// bin 0 = nearest, higher = farther. Called per live call so it sees the freshly-linked world faces
// (not the run-end, post-DrawOTag residual). The OT is frame-cumulative across renderers, so the
// histogram is the frame's depth distribution dominated by the world renderer; the per-call SZ3
// range (world_oracle_depth) is the world renderer's isolated depth sample.
//
// Chain format (func_80016784 / the emit link at 0x800265A4): each 8-byte bin's word 0 is the head
// packet address; each packet's low 24 bits of word 0 point to the next packet in the bin
// (sh fp,0($at); sb fp>>16,2($at)). Reconstruct the next link as 0x80000000 | (word0 & 0xFFFFFF).
void world_oracle_ot(Core *c) {
  const uint32_t base = c->mem_r32(0x80075820u); // g_WorldOT
  if (!ram_range(base, 0x4000u)) {
    s_world.ot_badbase++;
    return;
  }
  constexpr int kMaxWalk = 100000; // hard cap; a malformed chain must not hang a live call
  s_world.ot_walks++;
  for (int b = 0; b < 0x800; ++b) {
    uint32_t head = c->mem_r32(base + (uint32_t)b * 8u);
    if (!head) {
      continue;
    }
    if (!ram_range(head, 8u)) {
      continue;
    }
    s_world.ot_bins_used++;
    uint32_t p = head;
    int n = 0;
    while (p && n < kMaxWalk) {
      if (!ram_range(p, 8u)) {
        break;
      }
      const uint32_t link = c->mem_r32(p) & 0xFFFFFFu;
      n++;
      p = link ? (0x80000000u | link) : 0u;
    }
    if (n >= kMaxWalk) {
      s_world.ot_truncated++;
    }
    s_world.ot_bins[b] += (uint64_t)n;
  }
}

bool ram_range(uint32_t addr, uint32_t bytes) {
  // Asset relocation intentionally leaves some pointers in the physical-RAM alias (the renderer
  // itself masks bit 31 from its vertex pointer). Core::mem_r* accepts both aliases, so rejecting
  // low pointers here would call every valid streamed mesh corrupt.
  const uint32_t physical = addr & 0x1FFFFFFFu;
  return (addr < 0x00200000u || (addr >= kRamBegin && addr < kRamEnd)) && physical < 0x00200000u &&
         bytes <= 0x00200000u - physical;
}

void census_sprite_queue(Core *c) {
  const uint64_t prim_before = s_spriteq.primitives;
  const uint64_t screen_prim_before = s_spriteq.screen_primitives;
  s_spriteq.calls++;
  const uint32_t stage = c->mem_r32(0x800757D8u);
  const uint32_t mode = c->mem_r32(0x80078D78u);
  const bool classified = stage < 16u && mode < 4u;
  const uint32_t class_index = stage * 4u + mode;
  if (classified) {
    s_spriteq.stage_mode_calls[class_index]++;
  }
  uint32_t call_records = 0;
  bool terminated = false;
  for (uint32_t qi = 0; qi < kQueueCapacity; ++qi) {
    const uint32_t actor = c->mem_r32(kSpriteQueue + qi * 4u);
    if (!actor) {
      terminated = true;
      break;
    }
    if (!ram_range(actor, 0x58u)) {
      s_spriteq.invalid_actor++;
      continue;
    }
    call_records++;
    s_spriteq.records++;
    // The body tests `(u16(actor+0x50) << 24) < 0`: on little-endian MIPS that is the sign bit of
    // byte +0x50, not bit 15 of the halfword. Text actors write 0xFF to exactly this byte.
    const bool screen_space = (c->mem_r8(actor + 0x50u) & 0x80u) != 0;
    const uint16_t mesh_index = c->mem_r16(actor + 0x36u);
    if (screen_space) {
      s_spriteq.screen_records++;
    }
    if (screen_space) {
      s_spriteq.screen_mesh_records[mesh_index]++;
      if (s_spriteq.screen_mesh_records[mesh_index] == 1) {
        s_spriteq.screen_mesh_actor_flags[mesh_index] = c->mem_r32(actor + 0x4Cu);
      }
    }
    if (classified) {
      s_spriteq.stage_mode_records[class_index]++;
      if (screen_space) {
        s_spriteq.stage_mode_screen_records[class_index]++;
      }
    }

    if (!s_spriteq.mesh_seen[mesh_index]) {
      s_spriteq.mesh_seen[mesh_index] = true;
      s_spriteq.distinct_meshes++;
    }
    const uint32_t mesh = c->mem_r32(kActorMeshTable + (uint32_t)mesh_index * 4u);
    // The queue is built before visibility rejection. A null mesh entry is therefore an observed
    // input state, not corruption: the guest only dereferences it inside its visible branch.
    if (!mesh) {
      s_spriteq.absent_mesh++;
      continue;
    }
    if (!ram_range(mesh, 0x10u)) {
      s_spriteq.invalid_mesh++;
      continue;
    }
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
    if (!stream && primitive_count) {
      s_spriteq.absent_stream++;
      continue;
    }
    if (!ram_range(stream, primitive_count * 8u)) {
      s_spriteq.invalid_stream++;
      if (!s_spriteq.invalid_stream_seen[mesh_index]) {
        s_spriteq.invalid_stream_seen[mesh_index] = true;
        lucent::info("spriteq",
                     "unavailable stream state: mesh_index={} mesh=0x{:08X} "
                     "vertex_count={} primitive_count={} stream=0x{:08X}; queued records "
                     "are inspected before the guest's visibility branch",
                     mesh_index,
                     mesh,
                     vertex_count,
                     primitive_count,
                     stream);
      }
      continue;
    }

    for (uint32_t pi = 0; pi < primitive_count; ++pi) {
      const uint32_t packed = c->mem_r32(stream + pi * 8u);
      const uint32_t i0 = (packed >> 21u) & 0x1FCu;
      const uint32_t i1 = (packed >> 14u) & 0x1FCu;
      const uint32_t i2 = (packed >> 7u) & 0x1FCu;
      const uint32_t i3 = packed & 0x1FCu;
      if (i0 / 4u >= vertex_count || i1 / 4u >= vertex_count || i2 / 4u >= vertex_count ||
          i3 / 4u >= vertex_count) {
        s_spriteq.invalid_index++;
        continue;
      }
      s_spriteq.primitives++;
      if (screen_space) {
        s_spriteq.screen_primitives++;
      }
      if (screen_space) {
        s_spriteq.screen_mesh_primitives[mesh_index]++;
      }
      if ((packed & 3u) == 3u) {
        s_spriteq.bit11++;
        if (screen_space) {
          s_spriteq.screen_bit11++;
        }
        if (classified) {
          s_spriteq.stage_mode_variants[class_index * 4u + 0u]++;
        }
      } else if ((packed & 1u) != 0u) {
        s_spriteq.bit01++;
        if (screen_space) {
          s_spriteq.screen_bit01++;
        }
        if (classified) {
          s_spriteq.stage_mode_variants[class_index * 4u + 1u]++;
        }
      } else if (i2 != i3) {
        s_spriteq.gouraud_quad++;
        if (screen_space) {
          s_spriteq.screen_gouraud_quad++;
        }
        if (classified) {
          s_spriteq.stage_mode_variants[class_index * 4u + 2u]++;
        }
      } else {
        s_spriteq.gouraud_tri++;
        if (screen_space) {
          s_spriteq.screen_gouraud_tri++;
        }
        if (classified) {
          s_spriteq.stage_mode_variants[class_index * 4u + 3u]++;
        }
      }
    }
  }
  if (!terminated) {
    s_spriteq.unterminated_queues++;
  }
  if (!call_records) {
    s_spriteq.empty_calls++;
  }
  lucent::debug("spriteq",
                "reference queue input: present={} stage={} mode={} state={} timer={} "
                "records={} primitives={} screen_primitives={}",
                spyro_producer_run_present_count(),
                stage,
                mode,
                c->mem_r32(0x80078D7Cu),
                (int32_t)c->mem_r32(0x80078D80u),
                call_records,
                s_spriteq.primitives - prim_before,
                s_spriteq.screen_primitives - screen_prim_before);
}

void sprite_queue_hook(Core *c) {
  lucent::debug("spriteq",
                "reference queue: present={} stage={} mode={} state={} timer={}",
                spyro_producer_run_present_count(),
                c->mem_r32(0x800757D8u),
                c->mem_r32(0x80078D78u),
                c->mem_r32(0x80078D7Cu),
                (int32_t)c->mem_r32(0x80078D80u));
  census_sprite_queue(c);
  spyro_trace_reference_sprite_faces(c);
  const uint32_t pool_before = c->mem_r32(0x800757B0u);
  const RecompRegistry *R = psxport_recomp();
  R->shard_set_override(kSpriteRenderer, nullptr);
  R->main_dispatch(c, kSpriteRenderer);
  R->shard_set_override(kSpriteRenderer, sprite_queue_hook);
  const uint32_t pool_after = c->mem_r32(0x800757B0u);
  spyro_trace_reference_sprite_packets(c, pool_before, pool_after);
  uint32_t p = pool_before, packets = 0, tri = 0, quad = 0, malformed = 0;
  while (p < pool_after && packets < 4096u) {
    const uint32_t tag = c->mem_r32(p);
    const uint32_t words = tag >> 24;
    const uint32_t bytes = (words + 1u) * 4u;
    if (!words || bytes > pool_after - p) {
      malformed++;
      break;
    }
    const uint32_t op = c->mem_r8(p + 7u) & 0xFCu;
    if (op == 0x20u) {
      tri++;
    } else if (op == 0x28u) {
      quad++;
    }
    packets++;
    p += bytes;
  }
  lucent::debug("spriteq",
                "reference queue output: present={} timer={} pool=0x{:08X}..0x{:08X} "
                "packets={} tri={} quad={} malformed={} unparsed_bytes={}",
                spyro_producer_run_present_count(),
                (int32_t)c->mem_r32(0x80078D80u),
                pool_before,
                pool_after,
                packets,
                tri,
                quad,
                malformed,
                pool_after - p);
}

// ANY address, and now ANY NUMBER OF THEM — the remaining ownership queue is five renderers (C133)
// and the question "is this one actually called, and is it reproducible under the rewind?" has to
// be answered for each before choosing which to transcribe. Asking one per run costs a rebuild and
// a capture per address for an answer that a single run can give for all of them, and the arming
// log below prints the whole armed set so a silent typo cannot masquerade as "never called".
//
// The generated body cannot be named generically, so the probe re-dispatches: it steps out of its
// own override slot, dispatches the address (which now finds no override and runs the real body),
// and puts itself back. Same self-clearing trampoline fntrace uses, and for the same reason.
constexpr int kMaxProbes = 16;
uint32_t s_addrs[kMaxProbes];
char s_names[kMaxProbes][64];
int s_count = 0;

// Which address the CURRENTLY EXECUTING probe is for. ndiff calls `redispatch` synchronously from
// inside `ident_hook`, so a single current-address is enough — but it is saved and restored around
// the call because one renderer calling another (both armed) would otherwise leave the outer probe
// re-dispatching the INNER address, which does not fail loudly; it silently runs the wrong body.
uint32_t s_cur = 0;
void ident_hook(Core *c);

void redispatch(Core *c) {
  const RecompRegistry *R = psxport_recomp();
  const uint32_t a = s_cur;
  R->shard_set_override(a, nullptr);
  R->main_dispatch(c, a);
  R->shard_set_override(a, ident_hook);
}

// ndiff calls `native` first, rewinds, then calls `body`; handing it the SAME function twice asks
// only "is this function reproducible under the rewind?" — which is what has to be true before a
// reimplementation of it could ever be certified.
void ident_hook(Core *c) {
  const uint32_t addr = c->pc;
  int idx = -1;
  for (int i = 0; i < s_count; i++) {
    if (s_addrs[i] == addr) {
      idx = i;
      break;
    }
  }
  if (idx < 0) { // cannot happen unless the slot was armed for another address
    const RecompRegistry *R = psxport_recomp();
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

// ── MUTE: the one experiment that answers "what does this renderer actually DRAW" without
// inference.
//
// Twice in this project a renderer's visual contribution was reasoned about and got a wrong answer
// — once badly enough that a working OFX change was recorded as having "no effect" (issue 0039).
// What settled it was replacing the body with nothing and looking at what disappeared. That is a
// general question for every renderer in the ownership queue (which ones draw the 3D world and
// therefore need the projection re-centred, and which draw screen-space content that must NOT
// move), so it belongs here as a facility rather than as a temporary edit to whichever body is
// under the microscope.
//
// A muted body returns immediately: it writes no packets, links nothing into the ordering table,
// and does not run the register save/restore. That makes it a DIAGNOSTIC ONLY — the guest state it
// leaves behind is not the guest state the real body would leave — so it is loudly logged and never
// default.
void mute_hook(Core *) {}

// ── INTERPRET: can the flat interpreter stand in for a recompiled renderer, bit for bit?
//
// THE QUESTION BEHIND IT. The widescreen blocker is that every renderer's clip bounds are IMMEDIATE
// constants in its own instruction stream (0x02000000 = sx >= 512), so they cannot be moved while
// the guest owns the code — which is why the plan of record is to transcribe ~9150 instructions of
// hand-written assembly into native C. But the constants are immediates in GUEST RAM too, and the
// interpreter reads them from there rather than from a baked C literal. If interpreting a renderer
// is byte-identical to running its recompiled body, then a widened bound is a one-word change to
// guest memory instead of a thousand lines of transcription, and it stays honest: the code that
// runs is still the game's own, not a reimplementation standing in for it.
//
// This probe asks ONLY the first half — is the interpreted body exact? — because if it is not, the
// rest of the idea is dead and no patching is worth designing. It runs interpreted, then rewinds
// and runs the recompiled body, and reports any difference in RAM, the scratchpad, the GPRs or
// COP2.
uint32_t s_icur = 0;
char s_inames[kMaxProbes][64];
uint32_t s_iaddrs[kMaxProbes];
int s_icount = 0;

void interp_hook(Core *c);
void interp_side(Core *c) {
  interp_call(c, s_icur);
}

void interp_body(Core *c) {
  const RecompRegistry *R = psxport_recomp();
  const uint32_t a = s_icur;
  R->shard_set_override(a, nullptr);
  R->main_dispatch(c, a);
  R->shard_set_override(a, interp_hook);
}

void interp_hook(Core *c) {
  const uint32_t addr = c->pc;
  int idx = -1;
  for (int i = 0; i < s_icount; i++) {
    if (s_iaddrs[i] == addr) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    interp_call(c, addr);
    return;
  }
  const uint32_t saved = s_icur;
  s_icur = addr;
  ndiff_run(c, s_inames[idx], interp_side, interp_body);
  s_icur = saved;
}

} // namespace

void spyro_register_native_render() {
  if (cfg_str("PSXPORT_SPRITE_QUEUE_CENSUS")) {
    s_spriteq.armed = true;
    psxport_recomp()->shard_set_override(kSpriteRenderer, sprite_queue_hook);
    lucent::info("spriteq",
                 "ARMED input census at 0x{:08X}: queue capacity {}, scanning game actor + "
                 "mesh records before the unchanged guest renderer. The run-end report "
                 "prints calls and records even when both are zero.",
                 kSpriteRenderer,
                 kQueueCapacity);
  }
  spyro_world_animation_oracle_install();
  // PSXPORT_WORLD_CENSUS=1 — measure RenderWorldChunks 0x800258F0's game-state inputs at every live
  // call, then run the unchanged guest body. The run-end report (spyro_world_census_finish) prints
  // calls/chunks/pool-bytes even when the run never reached the field — zero is a loud answer, not
  // a silent one.
  if (cfg_str("PSXPORT_WORLD_CENSUS")) {
    // The census and the native world body claim the SAME single override slot for 0x800258F0, and
    // this registration runs last — so it would win silently, and the census would then measure the
    // guest body while the log said the native one was installed. Refuse instead of deciding by
    // registration order.
    if (cfg_on("PSXPORT_NATIVE_WORLD")) {
      lucent::error("worldcensus",
                    "PSXPORT_WORLD_CENSUS=1 and PSXPORT_NATIVE_WORLD=1 both claim the single "
                    "override slot at 0x{:08X}. NOT arming the census — it would displace the "
                    "native body and then report on the guest one. Pick one.",
                    kWorldRenderer);
    } else {
      s_world.armed = true;
      psxport_recomp()->shard_set_override(kWorldRenderer, world_hook);
      lucent::info("worldcensus",
                   "ARMED input census at 0x{:08X} (RenderWorldChunks): reading g_Environment's "
                   "chunk list + pool movement before the unchanged guest renderer. The run-end "
                   "report prints calls and entries even when both are zero.",
                   kWorldRenderer);
    }
  }
  // PSXPORT_MUTE_FN=<hex guest address>[,<hex>...] — replace these bodies with nothing.
  if (const char *m = cfg_str("PSXPORT_MUTE_FN")) {
    for (const char *p = m; *p;) {
      while (*p == ',' || *p == ' ') {
        p++;
      }
      if (!*p) {
        break;
      }
      char *end = nullptr;
      const uint32_t addr = (uint32_t)strtoul(p, &end, 16);
      if (end == p) {
        // `m` is non-null (checked above) and `p` points into it, so neither can be a null
        // `const char*` — the one std::format case printf would have survived and this would not.
        lucent::error("ndiff",
                      "PSXPORT_MUTE_FN={}: '{}' is not a hex guest address; NOTHING is muted "
                      "from here on",
                      m,
                      p);
        break;
      }
      p = end;
      if (!addr) {
        continue;
      }
      psxport_recomp()->shard_set_override(addr, mute_hook);
      lucent::info("ndiff",
                   "MUTE@0x{:08X} — this body is REPLACED BY NOTHING. Whatever disappears "
                   "from the frame is exactly its visual contribution. The run is "
                   "diagnostic: guest state this body would have written is simply absent.",
                   addr);
    }
  }
  // PSXPORT_INTERP_FN=<hex guest address>[,<hex>...] — run these bodies INTERPRETED, and (under
  // PSXPORT_NDIFF) verify each call against the recompiled body it replaces.
  if (const char *iv = cfg_str("PSXPORT_INTERP_FN")) {
    for (const char *p = iv; *p && s_icount < kMaxProbes;) {
      while (*p == ',' || *p == ' ') {
        p++;
      }
      if (!*p) {
        break;
      }
      char *end = nullptr;
      const uint32_t addr = (uint32_t)strtoul(p, &end, 16);
      if (end == p) {
        lucent::error("ndiff",
                      "PSXPORT_INTERP_FN={}: '{}' is not a hex guest address; NOTHING is "
                      "interpreted from here on",
                      iv,
                      p);
        break;
      }
      p = end;
      if (!addr) {
        continue;
      }
      s_iaddrs[s_icount] = addr;
      snprintf(s_inames[s_icount], sizeof s_inames[0], "INTERP@0x%08X", addr);
      psxport_recomp()->shard_set_override(addr, interp_hook);
      lucent::info("ndiff",
                   "{} ARMED — this body runs INTERPRETED from guest RAM instead of as "
                   "recompiled C. Under PSXPORT_NDIFF each call is compared against the "
                   "recompiled body; zero reported calls means it never ran, which is not "
                   "the same answer as 'it matched'.",
                   s_inames[s_icount]);
      s_icount++;
    }
  }

  // PSXPORT_NDIFF_IDENTITY=<hex guest address>[,<hex>...] — off unless asked for. Running any body
  // twice per call is far too expensive for a normal run, and this answers a one-off question per
  // renderer.
  const char *e = cfg_str("PSXPORT_NDIFF_IDENTITY");
  if (!e || !*e) {
    return;
  }
  for (const char *p = e; *p && s_count < kMaxProbes;) {
    while (*p == ',' || *p == ' ') {
      p++;
    }
    if (!*p) {
      break;
    }
    char *end = nullptr;
    const uint32_t addr = (uint32_t)strtoul(p, &end, 16);
    if (end == p) {
      // A silently-skipped token is how a probe reports "never called" for an address it never
      // armed.
      lucent::error("ndiff",
                    "PSXPORT_NDIFF_IDENTITY={}: '{}' is not a hex guest address (e.g. "
                    "8004F000); NOTHING is armed from here on",
                    e,
                    p);
      return;
    }
    p = end;
    if (!addr) {
      continue;
    }
    s_addrs[s_count] = addr;
    snprintf(s_names[s_count], sizeof s_names[0], "IDENTITY@0x%08X", addr);
    psxport_recomp()->shard_set_override(addr, ident_hook);
    s_count++;
  }
  if (!s_count) {
    lucent::error("ndiff", "PSXPORT_NDIFF_IDENTITY={} armed NO addresses", e);
    return;
  }
  for (int i = 0; i < s_count; i++) {
    lucent::info("ndiff",
                 "{} ARMED — running the generated body against itself. A divergence means "
                 "the differential CANNOT validate a function of this shape, and owning it "
                 "would need a different acceptance test. Zero calls means it never ran in "
                 "this capture, which is a different answer from 'it diverged'.",
                 s_names[i]);
  }
}

void spyro_sprite_queue_census_finish() {
  if (!s_spriteq.armed) {
    return;
  }
  lucent::info("spriteq",
               "CENSUS: calls={} empty_calls={} records={} distinct_meshes={} primitives={} "
               "variants(bit11={}, bit01={}, gouraud_quad={}, gouraud_tri={}) absent(mesh={}, "
               "stream={}) sentinel_mesh={} "
               "invalid(actor={}, mesh={}, stream={}, vertex_index={}) unterminated_queues={}",
               s_spriteq.calls,
               s_spriteq.empty_calls,
               s_spriteq.records,
               s_spriteq.distinct_meshes,
               s_spriteq.primitives,
               s_spriteq.bit11,
               s_spriteq.bit01,
               s_spriteq.gouraud_quad,
               s_spriteq.gouraud_tri,
               s_spriteq.absent_mesh,
               s_spriteq.absent_stream,
               s_spriteq.sentinel_mesh,
               s_spriteq.invalid_actor,
               s_spriteq.invalid_mesh,
               s_spriteq.invalid_stream,
               s_spriteq.invalid_index,
               s_spriteq.unterminated_queues);
  lucent::info("spriteq",
               "SCREEN-SPACE SUBSET: records={} primitives={} variants(bit11={}, bit01={}, "
               "gouraud_quad={}, gouraud_tri={})",
               s_spriteq.screen_records,
               s_spriteq.screen_primitives,
               s_spriteq.screen_bit11,
               s_spriteq.screen_bit01,
               s_spriteq.screen_gouraud_quad,
               s_spriteq.screen_gouraud_tri);
  for (uint32_t stage = 0; stage < 16u; ++stage) {
    for (uint32_t mode = 0; mode < 4u; ++mode) {
      const uint32_t i = stage * 4u + mode;
      if (!s_spriteq.stage_mode_calls[i]) {
        continue;
      }
      lucent::info("spriteq",
                   "CLASS stage={} mode={}: calls={} records={} screen_records={} variants("
                   "bit11={}, bit01={}, gouraud_quad={}, gouraud_tri={})",
                   stage,
                   mode,
                   s_spriteq.stage_mode_calls[i],
                   s_spriteq.stage_mode_records[i],
                   s_spriteq.stage_mode_screen_records[i],
                   s_spriteq.stage_mode_variants[i * 4u + 0u],
                   s_spriteq.stage_mode_variants[i * 4u + 1u],
                   s_spriteq.stage_mode_variants[i * 4u + 2u],
                   s_spriteq.stage_mode_variants[i * 4u + 3u]);
    }
  }
  for (uint32_t mesh = 0; mesh < 65536u; ++mesh) {
    if (!s_spriteq.screen_mesh_records[mesh]) {
      continue;
    }
    lucent::info("spriteq",
                 "SCREEN MESH {}: records={} primitives={} first_actor_4c=0x{:08X}",
                 mesh,
                 s_spriteq.screen_mesh_records[mesh],
                 s_spriteq.screen_mesh_primitives[mesh],
                 s_spriteq.screen_mesh_actor_flags[mesh]);
  }
}

void spyro_world_census_finish(Core *c) {
  if (!s_world.armed) {
    return;
  }
  // The denominator is every call, and every counter is printed even when zero — a run that never
  // reached the field (or never reached the world renderer) reads "calls=0", which is a loud
  // negative, not an empty pass.
  lucent::info("worldcensus",
               "CENSUS: calls={} (a0<0 flat-list={}, a0>=0 occlusion-group={}) "
               "flat_entries={} (max {}) occ_entries={} (max {}) pool_bytes={} "
               "pool_bytes_zero_calls={} last_occlusion_group={}",
               s_world.calls,
               s_world.negative_group,
               s_world.positive_group,
               s_world.flat_entries,
               s_world.max_flat,
               s_world.occ_entries,
               s_world.max_occ,
               s_world.pool_bytes,
               s_world.pool_bytes_zero,
               s_world.last_occlusion_group);
  // The ORACLE half — depth. Every counter prints even when zero, so a run that never reached the
  // field reads a loud negative rather than an empty pass.
  const uint64_t mean = s_world.sz3_samples ? s_world.sz3_total / s_world.sz3_samples : 0;
  lucent::info("worldcensus",
               "ORACLE: chunks={} vertices={} depth_calls={} sz3_seen={} "
               "sz3_range {}..{} (mean {}) pool(packets={} quads={} tris={} malformed={} "
               "unparsed_bytes={})",
               s_world.chunks,
               s_world.vertices,
               s_world.depth_calls,
               s_world.sz3_seen,
               s_world.sz3_seen ? s_world.sz3_min : 0,
               s_world.sz3_seen ? s_world.sz3_max : 0,
               mean,
               s_world.pool_packets,
               s_world.pool_quads,
               s_world.pool_tris,
               s_world.pool_malformed,
               s_world.pool_unparsed_bytes);
  // The accumulated per-face OT bin histogram — the "per-face OT bins" half of the oracle. Bins
  // 0..0x7FF = depth>>7, bin 0 nearest. Report the used-bin span, the faces in a few depth bands,
  // and a coarse per-region breakdown so the depth distribution is visible without a 2048-line
  // dump.
  uint64_t ot_total = 0, ot_bins_used = 0, ot_near = 0, ot_far = 0, ot_nearest = 0x7FF,
           ot_farthest = 0;
  for (int b = 0; b < 0x800; ++b) {
    const uint64_t f = s_world.ot_bins[b];
    if (!f) {
      continue;
    }
    ot_total += f;
    ot_bins_used++;
    if (b < (int)ot_nearest) {
      ot_nearest = b;
    }
    if (b > (int)ot_farthest) {
      ot_farthest = b;
    }
    if (b < 16) {
      ot_near += f;
    }
    if (b >= 0x3F0) {
      ot_far += f;
    }
  }
  lucent::info("worldcensus",
               "ORACLE-OT: walks={} bins_used={} faces={} nearest_bin={} farthest_bin={} "
               "near(0..15)={} far(0x3F0..0x7FF)={} badbase={} truncated={}",
               s_world.ot_walks,
               ot_bins_used,
               ot_total,
               ot_nearest,
               ot_farthest,
               ot_near,
               ot_far,
               s_world.ot_badbase,
               s_world.ot_truncated);
  for (int b = 0; b < 0x800; b += 64) {
    uint64_t band = 0;
    for (int j = 0; j < 64; ++j) {
      band += s_world.ot_bins[b + j];
    }
    if (band) {
      lucent::info("worldcensus", "ORACLE-OT band {:04X}..{:04X}: {} faces", b, b + 63, band);
    }
  }
}
