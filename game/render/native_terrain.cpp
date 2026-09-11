// native_terrain.cpp — Spyro's terrain renderer (0x8004EBA8), owned natively.
#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "painter_submission_preflight.h"
#include "producer_scope.h"
#include "proj_params.h"
#include "proj_vtx.h"
#include "render_queue.h"
#include "scene_painter_order.h"
#include "spyro_game.h"
#include "wide_clip_plan.h"

#include <array>
#include <cstdint>
#include <lucent/log.h>
#include <vector>

void proj_native_xform(int, int, int, ProjVtx *);

namespace {

constexpr uint32_t kObjListSel = 0x80078A40u;
constexpr uint32_t kPoolPtr = 0x800757B0u;
constexpr uint32_t kPoolLimit = 0x80075780u;
constexpr uint32_t kProducerKey = 0x8004EBA8u;
constexpr uint32_t kTagSub = 0x10000000u;

struct TerrainDirectVertex {
  ProjVtx p{};
  float rawX = 0, rawY = 0, rawZ = 0;
  uint32_t clip = 0;
};

struct TerrainDirectFace {
  uint32_t object = 0, source = 0;
  bool gouraud = false;
  std::array<TerrainDirectVertex, 3> v{};
  std::array<uint32_t, 3> rgb{};
};

struct TerrainDirectRecipe {
  std::vector<TerrainDirectFace> faces;
  uint32_t objects = 0, candidates = 0, rejects = 0, f3 = 0, g3 = 0, vertices = 0;
  const char *refusal = "none";
};

struct TerrainGteGuard {
  std::array<uint32_t, 32> dr{}, cr{};
  ProjParams *pp = nullptr;
  ProjParams::Snapshot proj{};
  explicit TerrainGteGuard(Core *c = nullptr) {
    for (uint32_t i = 0; i < 32; ++i) {
      dr[i] = gte_read_data(i);
      cr[i] = gte_read_ctrl(i);
    }
    if (c) {
      pp = &c->rsub.projParams;
      proj = pp->snapshot();
    }
  }
  ~TerrainGteGuard() {
    for (uint32_t i = 0; i < 32; ++i) {
      gte_write_data(i, dr[i]);
      gte_write_ctrl(i, cr[i]);
    }
    if (pp) {
      pp->restore(proj);
    }
  }
};

bool terrain_ram(uint32_t a, uint32_t n) {
  const uint32_t p = a & 0x1FFFFFFFu;
  return (a < 0x00200000u || (a >= 0x80000000u && a < 0x80200000u)) && p <= 0x200000u &&
         n <= 0x200000u - p;
}

int64_t terrain_wrap44(int64_t v) {
  return (int64_t)((uint64_t)v << 20) >> 20;
}

void terrain_raw_xyz(int vx, int vy, int vz, float &x, float &y, float &z) {
  const uint32_t c0 = gte_read_ctrl(0), c1 = gte_read_ctrl(1), c2 = gte_read_ctrl(2),
                 c3 = gte_read_ctrl(3), c4 = gte_read_ctrl(4);
  const int32_t m[3][3] = {{(int16_t)c0, (int16_t)(c0 >> 16), (int16_t)c1},
                           {(int16_t)(c1 >> 16), (int16_t)c2, (int16_t)(c2 >> 16)},
                           {(int16_t)c3, (int16_t)(c3 >> 16), (int16_t)c4}};
  const int32_t tr[3] = {
      (int32_t)gte_read_ctrl(5), (int32_t)gte_read_ctrl(6), (int32_t)gte_read_ctrl(7)};
  const int16_t v[3] = {(int16_t)vx, (int16_t)vy, (int16_t)vz};
  float *o[3] = {&x, &y, &z};
  for (int r = 0; r < 3; ++r) {
    int64_t t = (int64_t)tr[r] << 12;
    t = terrain_wrap44(t + (int64_t)m[r][0] * v[0]);
    t = terrain_wrap44(t + (int64_t)m[r][1] * v[1]);
    t = terrain_wrap44(t + (int64_t)m[r][2] * v[2]);
    *o[r] = (float)t / 4096.0f;
  }
}

TerrainDirectVertex terrain_project(int vx, int vy, int vz) {
  TerrainDirectVertex out{};
  terrain_raw_xyz(vx, vy, vz, out.rawX, out.rawY, out.rawZ);
  proj_native_xform(vx, vy, vz, &out.p);
  return out;
}

bool terrain_build_direct(Core *c,
                          int32_t selector,
                          const std::array<uint32_t, 5> &mat1,
                          const std::array<uint32_t, 5> &mat2,
                          TerrainDirectRecipe &out) {
  auto refuse = [&](const char *why) {
    out.refusal = why;
    out.faces.clear();
    return false;
  };
  int32_t rightClip = spyro::wide::kNativeClipWidth;
  if (gpu_vk_wide_engine(c)) {
    const int nw = gpu_vk_wide_engine_w(c);
    rightClip = nw;
    gte_write_ctrl(24u, (uint32_t)((nw / 2) << 16));
    c->rsub.projParams.setGeomOfxForAspect((float)(nw / 2));
  }
  auto load_matrix = [&](const std::array<uint32_t, 5> &words) {
    for (uint32_t i = 0; i < 5; ++i) {
      gte_write_ctrl(i, words[i]);
    }
    gte_write_ctrl(5, 0);
    gte_write_ctrl(6, 0);
    gte_write_ctrl(7, 0);
  };
  load_matrix(mat1);
  std::vector<uint32_t> survivors;
  survivors.reserve(256);
  uint32_t listBase = c->mem_r32(kObjListSel + 4), cursor = 0, end = 0;
  if (selector < 0) {
    const uint32_t count = c->mem_r32(kObjListSel);
    cursor = listBase;
    end = listBase + (count << 2);
  } else {
    const uint32_t table = c->mem_r32(kObjListSel + 12);
    if (!terrain_ram(table + (uint32_t)selector * 4, 4)) {
      return refuse("selector_bounds");
    }
    cursor = c->mem_r32(table + (uint32_t)selector * 4);
  }
  for (uint32_t guard = 0; guard < 4096; ++guard) {
    uint32_t obj = 0;
    if (selector < 0) {
      if (cursor == end) {
        break;
      }
      if (!terrain_ram(cursor, 4)) {
        return refuse("object_list_bounds");
      }
      obj = c->mem_r32(cursor);
      cursor += 4;
    } else {
      if (!terrain_ram(cursor, 1)) {
        return refuse("object_index_bounds");
      }
      const uint8_t ix = c->mem_r8(cursor++);
      if (ix == 255) {
        break;
      }
      if (!terrain_ram(listBase + (uint32_t)ix * 4, 4)) {
        return refuse("object_index_target");
      }
      obj = c->mem_r32(listBase + (uint32_t)ix * 4);
    }
    if (!terrain_ram(obj, 24)) {
      return refuse("object_bounds");
    }
    const uint32_t xy = c->mem_r32(obj), zz = c->mem_r32(obj + 4);
    auto p = terrain_project((int16_t)xy, (int16_t)(xy >> 16), (int16_t)(zz >> 16));
    const int32_t limit = (int16_t)zz;
    if ((int32_t)((uint32_t)(int32_t)p.rawZ - (uint32_t)limit) > 0) {
      survivors.push_back(obj);
    }
    if (selector < 0 && cursor == end) {
      break;
    }
    if (guard == 4095) {
      return refuse("object_list_unterminated");
    }
  }
  load_matrix(mat2);
  uint32_t virtualFp = c->mem_r32(kPoolPtr) + 4u;
  const uint32_t poolEnd = c->mem_r32(kPoolLimit) - 1024u;
  for (uint32_t obj : survivors) {
    ++out.objects;
    const uint32_t originXY = c->mem_r32(obj + 8), meta = c->mem_r32(obj + 12),
                   faceMeta = c->mem_r32(obj + 16);
    const int32_t oy = (int16_t)originXY, ox = (int16_t)(originXY >> 16),
                  oz = (int16_t)(meta >> 16);
    const uint32_t vertexCount = (meta & 0xFFFFu) + 1u;
    if (vertexCount >= 1024u || !terrain_ram(obj + 24, (vertexCount + 1u) * 4u)) {
      return refuse("vertex_span");
    }
    std::vector<TerrainDirectVertex> vertices;
    vertices.reserve(vertexCount);
    uint32_t all = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < vertexCount; ++i) {
      const uint32_t w = c->mem_r32(obj + 24 + i * 4u);
      const uint32_t packed =
          (uint32_t)(oy - (int)((w >> 10) & 0x7FFu)) + ((uint32_t)(ox - (int)(w & 0x3FFu)) << 16);
      const int vx = (int16_t)packed, vy = (int16_t)(packed >> 16),
                vz = (int16_t)((uint32_t)(w >> 21) + (uint32_t)oz);
      auto p = terrain_project(vx, vy, vz);
      p.clip = spyro::wide::clipCode(p.p.sx, p.p.sy, rightClip);
      all &= p.clip;
      vertices.push_back(p);
      ++out.vertices;
    }
    if (all & 0xFu) {
      continue;
    }
    const uint32_t colorBase = obj + 24 + (vertexCount - 1u) * 4u;
    const uint32_t faceBegin = colorBase + (faceMeta >> 14), faceBytes = (faceMeta << 3) & 0xFFF8u;
    if (!terrain_ram(faceBegin, faceBytes)) {
      return refuse("face_span");
    }
    for (uint32_t a = faceBegin; a < faceBegin + faceBytes; a += 8) {
      ++out.candidates;
      const uint32_t fw = c->mem_r32(a), mw = c->mem_r32(a + 4);
      const uint32_t vi[3] = {fw >> 20, (fw >> 10) & 0x3FCu, fw & 0x3FCu};
      uint32_t ix[3]{}, clips[3]{};
      for (int i = 0; i < 3; ++i) {
        if (vi[i] & 3u) {
          return refuse("vertex_index_alignment");
        }
        ix[i] = vi[i] / 4u;
        if (ix[i] >= vertices.size()) {
          // Native acceptance cannot depend on projected scratch from a guest renderer.
          return refuse("external_vertex_source_unowned");
        }
        clips[i] = vertices[ix[i]].clip;
      }
      if (clips[0] & clips[1] & clips[2] & 0x1Fu) {
        ++out.rejects;
        continue;
      }
      const uint32_t co[3] = {mw >> 20, (mw >> 10) & 0x3FCu, mw & 0x3FCu};
      TerrainDirectFace f{};
      f.object = obj;
      f.source = a;
      f.gouraud = !(co[0] == co[1] && co[0] == co[2]);
      const uint32_t stride = f.gouraud ? 28u : 20u;
      if ((int32_t)(poolEnd - virtualFp) <= 0) {
        return refuse("pool_exhaustion_equivalent");
      }
      virtualFp += stride;
      for (int i = 0; i < 3; ++i) {
        if (!terrain_ram(colorBase + co[i], 4)) {
          return refuse("color_index");
        }
        f.v[i] = vertices[ix[i]];
        f.rgb[i] = c->mem_r32(colorBase + co[i]);
      }
      if (!f.gouraud) {
        for (auto &rgb : f.rgb) {
          rgb -= kTagSub;
        }
      }
      out.faces.push_back(f);
      f.gouraud ? ++out.g3 : ++out.f3;
    }
  }
  return true;
}

bool terrain_submit_direct(Core *c,
                           int32_t selector,
                           const std::array<uint32_t, 5> &mat1,
                           const std::array<uint32_t, 5> &mat2) {
  TerrainGteGuard preserveGte(c);
  TerrainDirectRecipe recipe{};
  if (!terrain_build_direct(c, selector, mat1, mat2, recipe)) {
    lucent::error("terraindirect",
                  "REFUSED objects={} candidates={} rejects={} F3={} G3={} faces={} first={}",
                  recipe.objects,
                  recipe.candidates,
                  recipe.rejects,
                  recipe.f3,
                  recipe.g3,
                  recipe.faces.size(),
                  recipe.refusal);
    return false;
  }
  RenderQueue &rq = c->game->rq;
  if (recipe.faces.empty()) {
    lucent::debug("terraindirect",
                  "owned valid-empty objects={} candidates={} rejects={}",
                  recipe.objects,
                  recipe.candidates,
                  recipe.rejects);
    return true;
  }
  const auto plan = spyro::painter_submission::preflight(
      rq, kProducerKey, recipe.faces.size(), spyro::scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.ready) {
    return false;
  }
  const uint32_t baseSeq = rq.consumed ? 0u : rq.seq;
  if (recipe.faces.size() - 1u > UINT32_MAX - baseSeq) {
    return false;
  }
  const uint32_t finalSeq = baseSeq + (uint32_t)recipe.faces.size() - 1u;
  if ((plan.queued || plan.existingObjects) && !gpu_vk_order_bias_distinguishes(finalSeq)) {
    return false;
  }
  const GpuState gpu = c->game->gpu;
  if (gpu.s_da_x0 > gpu.s_da_x1 || gpu.s_da_y0 > gpu.s_da_y1) {
    return false;
  }
  const int da_x1 =
      gpu_vk_wide_engine(c) ? (gpu.s_da_x0 + gpu_vk_wide_engine_w(c) - 1) : gpu.s_da_x1;
  ProducerScope producer(&c->rsub.producerScope, kProducerKey, "terrain:direct");
  RenderQueue::PainterObjectScope painter(rq, kProducerKey);
  for (size_t faceIndex = 0; faceIndex < recipe.faces.size(); ++faceIndex) {
    const auto &f = recipe.faces[faceIndex];
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float xf[4]{}, yf[4]{}, depth[4]{};
    unsigned char rs[4]{}, gs[4]{}, bs[4]{};
    for (int i = 0; i < 3; ++i) {
      xs[i] = f.v[i].p.sx + gpu.s_off_x;
      ys[i] = f.v[i].p.sy + gpu.s_off_y;
      xf[i] = f.v[i].p.px + gpu.s_off_x;
      yf[i] = f.v[i].p.py + gpu.s_off_y;
      rs[i] = (uint8_t)f.rgb[i];
      gs[i] = (uint8_t)(f.rgb[i] >> 8);
      bs[i] = (uint8_t)(f.rgb[i] >> 16);
      depth[i] = proj_pz_to_ord(f.v[i].p.pz);
    }
    rq.emitOrQueue(c,
                   1,
                   RQ_WORLD,
                   RQ_OM_DEPTH,
                   3,
                   0,
                   0,
                   xs,
                   ys,
                   xf,
                   yf,
                   us,
                   vs,
                   rs,
                   gs,
                   bs,
                   depth,
                   3,
                   0,
                   0,
                   0,
                   0,
                   gpu.s_tw_mx,
                   gpu.s_tw_my,
                   gpu.s_tw_ox,
                   gpu.s_tw_oy,
                   gpu.s_da_x0,
                   gpu.s_da_y0,
                   da_x1,
                   gpu.s_da_y1,
                   0,
                   nullptr,
                   -1,
                   0.0f,
                   f.gouraud ? 1 : 0,
                   gpu.s_tp_dither ? 1 : 0,
                   spyro::scene_painter_order::cyclorama((uint32_t)faceIndex));
  }
  uint32_t grouped = 0;
  for (int i = 0; i < rq.n; ++i) {
    if (rq.items[i].painter_object == 0x8004EBA8u) {
      ++grouped;
    }
  }
  if (grouped != recipe.faces.size()) {
    lucent::error("terraindirect", "FATAL grouped={}/{}", grouped, recipe.faces.size());
    abort();
  }
  return true;
}

} // namespace

namespace {

// The guest passes two SHORTMATRIX POINTERS; the renderer itself only ever needs their five packed
// words. Reading them here keeps the bounds check at the one boundary that has an address to check.
std::optional<std::array<uint32_t, 5>> readMatrixWords(Core *c, uint32_t address) {
  if (!terrain_ram(address, 20)) {
    return std::nullopt;
  }
  std::array<uint32_t, 5> words{};
  for (uint32_t i = 0; i < words.size(); ++i) {
    words[i] = c->mem_r32(address + i * 4u);
  }
  return words;
}

} // namespace

bool spyro_terrain_submit(Core *c, int32_t selector, uint32_t mat1, uint32_t mat2) {
  const auto view = readMatrixWords(c, mat1);
  const auto projection = readMatrixWords(c, mat2);
  if (!view || !projection) {
    lucent::error(
        "terraindirect", "REFUSED matrix_bounds view=0x{:08X} projection=0x{:08X}", mat1, mat2);
    return false;
  }
  return terrain_submit_direct(c, selector, *view, *projection);
}

bool spyro_terrain_submit_matrices(Core *c,
                                   int32_t selector,
                                   const std::array<uint32_t, 5> &view,
                                   const std::array<uint32_t, 5> &projection) {
  return terrain_submit_direct(c, selector, view, projection);
}
