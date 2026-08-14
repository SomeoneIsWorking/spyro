// native_terrain.cpp — Spyro's terrain renderer (0x8004EBA8), owned natively.
//
// WHY THIS FUNCTION, AND WHY OWNERSHIP RATHER THAN OBSERVATION. Widescreen and 60fps both need the
// world's geometry under our control: the guest trivially rejects faces against clip bounds that are
// IMMEDIATE constants in its own instruction stream (right edge = 512<<16 at 0x8004ED8C), so the
// projection centre and the bounds can never be moved together while the guest owns this code. Native
// depth hit the same wall from the other side — observing a multi-hop staging pipeline from outside
// plateaued at 2.5% coverage. See re-frontier render.own-geometry-family and claims C127/C128.
//
// THE SHAPE, from the disassembly (scratch/logs/terrain.txt, now GTE-decoded):
//   1. save every callee-saved register to a FIXED area at 0x80077DD8 — no stack frame at all. This
//      idiom marks the 19 hand-written assembly renderers in this game.
//   2. load a 3x3 rotation matrix from a1 into the GTE, translation zeroed.
//   3. walk an object list; for each object RTPS its origin and reject it by DISTANCE, comparing the
//      GTE's MAC3 against a per-object threshold. Survivors are written to a work list at 0x8006FCF4.
//   4. load a SECOND rotation matrix from a2, then for each surviving object:
//        a. unpack its vertices — 11/11/10-bit packed deltas against a per-object origin — RTPS each
//           one, and write (screen XY << 5) | clip-code into a SCRATCHPAD vertex cache. The loop is
//           SOFTWARE PIPELINED: it reads vertex N's result while vertex N+1 is already in the GTE.
//        b. if every vertex shares an off-screen side, skip the object entirely.
//        c. walk the face list, index three cached vertices by pre-scaled byte offsets, reject the
//           face if the three clip codes share a side, then unshift and emit F3 (stride 0x14) or
//           untextured Gouraud G3 (stride 0x1C) packets into the pool.
//   5. publish the pool pointer and link the batch into the display list.
//
// BYTE-EXACT IS THE ADMISSION REQUIREMENT, not a stretch goal. ndiff snapshots RAM, the scratchpad,
// every GPR and the COP2 register file, runs this body, rewinds, runs the recompiled body, and
// compares. So EVERY register this function leaves behind is part of the contract — including the
// scratch ones no caller could sensibly read — and so is every byte it writes. An identity probe
// already proved the harness can validate a function of this shape (C129, 8/8 exact).
//
// THE CLIP BOUNDS ARE NAMED HERE, which is the entire point of owning it: kClipRight is the guest's
// 512<<16, and once this body is verified identical, widening is a one-constant change with a
// differential that will tell us exactly what moved.
#include "core.h"
#include "recomp_iface.h"
#include "rec_decls.h"
#include "native_diff.h"
#include "cfg.h"      // cfg_on — PSXPORT_NATIVE_TERRAIN is a feature flag, not a diagnostic
#include <lucent/log.h>
#include "spyro_game.h"
#include <array>
#include <cstring>
#include <vector>

// psxport's widescreen state: whether a wider aspect is selected, and the wide native width (which
// now scales from this game's own 512-wide 4:3 frame rather than a hardcoded 320).
int gpu_vk_wide_engine(Core*);
int gpu_vk_wide_engine_w(Core*);

namespace {

// ── GTE plumbing. The recompiler emits these same calls, so using them keeps the COP2 register file
//    bit-identical rather than merely equivalent.
constexpr uint32_t kRTPS = 0x4A180001u;      // RTPS with sf=1 (the 12-bit fractional shift)

// COP2 control registers (rotation matrix + translation).
enum { CR_R11R12 = 0, CR_R13R21 = 1, CR_R22R23 = 2, CR_R31R32 = 3, CR_R33 = 4,
       CR_TRX = 5, CR_TRY = 6, CR_TRZ = 7, CR_OFX = 24 };
// COP2 data registers.
enum { DR_VXY0 = 0, DR_VZ0 = 1, DR_SXY2 = 14, DR_SZ3 = 19, DR_MAC3 = 27 };

// ── Guest globals this renderer reads and writes, named rather than repeated as raw addresses.
constexpr uint32_t kObjListSel   = 0x80078A40u;   // + 4 / + 0xC: the three object-list roots
constexpr uint32_t kWorkList     = 0x8006FCF4u;   // survivors of the distance pass, NUL-terminated
constexpr uint32_t kPoolPtr      = 0x800757B0u;   // packet-pool write pointer
constexpr uint32_t kPoolLimit    = 0x80075780u;   // pool end (a 1024-byte margin is kept)
constexpr uint32_t kPoolOverflow = 0x800758B0u;   // set to 1 when the pool ran out
constexpr uint32_t kOtBase       = 0x80075820u;   // ordering table, linked at +16376 / +16380
constexpr uint32_t kSaveArea     = 0x80077DD8u;   // the fixed register-save block (no stack frame)
constexpr uint32_t kScratchpad   = 0x1F800000u;   // the per-object vertex cache lives here

// ── The clip bounds, as the guest hard-codes them. THESE ARE THE REASON THIS FILE EXISTS.
// Each is compared against the PACKED screen word (sy in the high half, sx in the low half), which is
// why the vertical bounds look like whole-word constants: subtracting 1<<16 tests sy, and shifting sx
// into the high half tests sx.
constexpr uint32_t kClipTop    = 0x00010000u;   //   sy <= 0        -> bit 1
constexpr uint32_t kClipBottom = 0x01000000u;   //   sy >= 256      -> bit 2
constexpr uint32_t kClipRight  = 0x02000000u;   //   sx >= 512      -> bit 8   (widescreen moves THIS)
// The left bound is an implicit `sx <= 0` (bit 4) — a comparison against zero, with no constant.

// Packet tags the face loop stamps into the OT link word.
constexpr uint32_t kTagF3  = 0x84000000u;   // 4 words of payload -> stride 0x14
constexpr uint32_t kTagG3 = 0x86000000u;    // 6 words of payload -> stride 0x1C
constexpr uint32_t kTagSub = 0x10000000u;   // subtracted from the fetched colour word

struct TerrainObservedFace {
  uint32_t object=0,source=0,pool=0;
  bool gouraud=false;
  std::array<uint32_t,3> xy{},rgb{};
};
struct TerrainOracleCapture {
  std::vector<TerrainObservedFace> emitted;
  TerrainObservedFace pending{};
  bool hasPending=false,poolRejected=false;
  uint32_t objects=0,candidates=0,clipRejects=0,f3=0,g3=0,other=0,poolRejects=0;
  uint32_t currentObject=0;
  uint32_t checkpoints=0,bytesCompared=0,mismatched=0,maxFaces=0;
  uint32_t poolStart=0,otBase=0,oldTail=0,oldHead=0,chainCompared=0,spliceCompared=0;
  const char* first="none";
};
static void terrain_bad(TerrainOracleCapture& o,const char* field){++o.mismatched;if(!std::strcmp(o.first,"none"))o.first=field;}
static void terrain_finish_candidate(Core* c,TerrainOracleCapture& o){
  if(!o.hasPending)return;
  const uint32_t delta=c->r[30]-o.pending.pool;
  if(delta==0){++o.clipRejects;}
  else if(delta==20u&&!o.pending.gouraud){++o.f3;o.emitted.push_back(o.pending);}
  else if(delta==28u&&o.pending.gouraud){++o.g3;o.emitted.push_back(o.pending);}
  else terrain_bad(o,"cursor_delta");
  o.hasPending=false;
}
static void terrain_checkpoint(Core* c,uint64_t,uint32_t pc,void* user){
  auto& o=*static_cast<TerrainOracleCapture*>(user);++o.checkpoints;
  if(pc==0x8004EF68u){o.hasPending=false;o.poolRejected=true;++o.poolRejects;return;}
  if(pc==0x8004EF78u){terrain_finish_candidate(c,o);return;}
  if(pc==0x8004ED44u){terrain_finish_candidate(c,o);const uint32_t next=c->mem_r32(c->r[31]);
    if(next){o.currentObject=next;++o.objects;}return;}
  if(pc!=0x8004EE84u)return;
  terrain_finish_candidate(c,o);
  if(c->r[21]==c->r[22])return;
  TerrainObservedFace f{};f.object=o.currentObject;f.source=c->r[21];++o.candidates;f.pool=c->r[30];
  const uint32_t face=c->mem_r32(c->r[21]),material=c->mem_r32(c->r[21]+4u);
  const uint32_t vo[3]={face>>20,(face>>10)&0x3FCu,face&0x3FCu};
  const uint32_t co[3]={material>>20,(material>>10)&0x3FCu,material&0x3FCu};
  for(int i=0;i<3;++i)f.xy[i]=(uint32_t)((int32_t)c->mem_r32(c->r[23]+vo[i])>>5);
  f.gouraud=!(co[0]==co[1]&&co[0]==co[2]);
  for(int i=0;i<3;++i)f.rgb[i]=c->mem_r32(c->r[20]+co[i]);
  if(!f.gouraud)for(int i=0;i<3;++i)f.rgb[i]-=kTagSub;
  o.pending=f;o.hasPending=true;
}
template<class Read> static bool terrain_compare_read(TerrainOracleCapture& o,Read read){
  for(const auto& f:o.emitted){const uint32_t words=(read(f.pool)>>24)&0x7Fu,cmd=read(f.pool+4u)>>24;
    if(words!=(f.gouraud?6u:4u))terrain_bad(o,"tag_words");
    if(cmd!=(f.gouraud?0x30u:0x20u)){++o.other;terrain_bad(o,"command");}
    if(f.gouraud){for(int i=0;i<3;++i){if(read(f.pool+4u+i*8u)!=f.rgb[i])terrain_bad(o,"rgb");if(read(f.pool+8u+i*8u)!=f.xy[i])terrain_bad(o,"xy");}o.bytesCompared+=28u;}
    else {if(read(f.pool+4u)!=f.rgb[0])terrain_bad(o,"rgb");for(int i=0;i<3;++i)if(read(f.pool+8u+i*4u)!=f.xy[i])terrain_bad(o,"xy");o.bytesCompared+=20u;}}
  o.maxFaces=(uint32_t)o.emitted.size();return o.mismatched==0;
}
static bool terrain_compare_packets(Core* c,TerrainOracleCapture& o){return terrain_compare_read(o,[&](uint32_t a){return c->mem_r32(a);});}
template<class Read> static bool terrain_compare_chain_read(TerrainOracleCapture& o,Read read){
  for(size_t i=0;i<o.emitted.size();++i){const uint32_t tag=read(o.emitted[i].pool);
    const uint32_t expected=i+1<o.emitted.size()?o.emitted[i+1].pool:0u;
    if((tag&0x00FFFFFFu)!=(expected&0x00FFFFFFu))terrain_bad(o,"packet_link");++o.chainCompared;}
  if(o.emitted.empty())return o.mismatched==0;
  const uint32_t newTail=o.emitted.back().pool,newHead=o.emitted.front().pool;
  if(newHead!=o.poolStart+4u)terrain_bad(o,"pool_first");
  if(read(o.otBase+16376u)!=newTail)terrain_bad(o,"ot_tail");
  if(!o.oldTail){if(read(o.otBase+16380u)!=newHead)terrain_bad(o,"ot_head");}
  else {if(read(o.otBase+16380u)!=o.oldHead)terrain_bad(o,"ot_head_preserve");
    if((read(o.oldTail)&0x00FFFFFFu)!=(newHead&0x00FFFFFFu))terrain_bad(o,"ot_append");}
  o.spliceCompared=1;return o.mismatched==0;
}
static bool terrain_compare_chain(Core* c,TerrainOracleCapture& o){return terrain_compare_chain_read(o,[&](uint32_t a){return c->mem_r32(a);});}

void terrain_native(Core* c) {
  uint32_t* R = c->r;

  // 1. Save the callee-saved registers to the fixed area. Reproduced faithfully because the epilogue
  //    reads them back — and because ndiff compares this memory.
  for (int i = 0; i < 8; i++) c->mem_w32(kSaveArea + 4 * i, R[16 + i]);   // s0-s7
  c->mem_w32(kSaveArea + 32, R[28]);                                      // gp
  c->mem_w32(kSaveArea + 36, R[29]);                                      // sp
  c->mem_w32(kSaveArea + 40, R[30]);                                      // fp
  c->mem_w32(kSaveArea + 44, R[31]);                                      // ra

  const uint32_t arg_sel = R[4], mat1 = R[5], mat2 = R[6];

  // 2. First rotation matrix, translation zeroed (gte_SetRotMatrix + a zero trans vector).
  uint32_t t3 = c->mem_r32(mat1 + 0), t4 = c->mem_r32(mat1 + 4), t5 = c->mem_r32(mat1 + 8);
  uint32_t t6 = c->mem_r32(mat1 + 12), t7 = c->mem_r32(mat1 + 16);
  gte_write_ctrl(CR_R11R12, t3); gte_write_ctrl(CR_R13R21, t4); gte_write_ctrl(CR_R22R23, t5);
  gte_write_ctrl(CR_R31R32, t6); gte_write_ctrl(CR_R33, t7);
  gte_write_ctrl(CR_TRX, 0);     gte_write_ctrl(CR_TRY, 0);     gte_write_ctrl(CR_TRZ, 0);

  // 3. Pick the object list. A negative selector means "the whole list"; otherwise it indexes a table.
  uint32_t at = kObjListSel;
  t7 = arg_sel;
  t5 = kWorkList;
  uint32_t v0, v1;
  t3 = c->mem_r32(kObjListSel + 4);
  if ((int32_t)t7 < 0) {
    at = c->mem_r32(kObjListSel);
    v1 = 0;                                   // `sll v1, t7, 2` never runs on this path
    at = at << 2;
    t4 = t3 + at;
  } else {
    v0 = c->mem_r32(kObjListSel + 12);
    v1 = t7 << 2;
    v0 = v0 + v1;
    t4 = c->mem_r32(v0);
  }

  // 3b. Distance pass: RTPS each candidate's origin and keep the ones inside its own threshold.
  for (;;) {
    if ((int32_t)t7 < 0) {
      if (t3 == t4) break;
      t6 = c->mem_r32(t3);
      t3 += 4;
    } else {
      at = c->mem_r8(t4);
      if (at == 255) break;
      at = t3 + (at << 2);
      t6 = c->mem_r32(at);
      t4 += 1;
    }
    at = c->mem_r32(t6 + 0);
    v0 = c->mem_r32(t6 + 4);
    gte_write_data(DR_VXY0, at);
    at = (uint32_t)((int32_t)v0 >> 16);
    gte_write_data(DR_VZ0, at);
    at = (uint32_t)((int32_t)(v0 << 16) >> 16);
    gte_op(c, kRTPS);
    v1 = c->mem_r32(t6 + 20);
    v0 = gte_read_data(DR_SZ3);
    v0 = gte_read_data(DR_MAC3);
    v0 = v0 - at;                              // distance beyond this object's threshold
    if ((int32_t)v0 <= 0) { R[4] = v1 + 1; continue; }
    // KEPT. Both arms of the guest's `beq a0,zero` continue the loop — the branch only skips a
    // redundant `j` — and `addi t5,t5,4` is that branch's DELAY SLOT, so the work-list pointer
    // advances EITHER WAY. Gating it on the branch made every object overwrite the same slot, so the
    // list ended up empty however many objects passed the distance test.
    uint32_t keep = v1 + 1;
    c->mem_w32(t5, t6);
    R[4] = keep;
    t5 += 4;
  }
  c->mem_w32(t5, 0);                            // terminate the work list

  // 4. Second rotation matrix — the one the vertices are actually projected with.
  at = c->mem_r32(mat2 + 0);  v0 = c->mem_r32(mat2 + 4);  v1 = c->mem_r32(mat2 + 8);
  uint32_t a0 = c->mem_r32(mat2 + 12), a1 = c->mem_r32(mat2 + 16);
  gte_write_ctrl(CR_R11R12, at); gte_write_ctrl(CR_R13R21, v0); gte_write_ctrl(CR_R22R23, v1);
  gte_write_ctrl(CR_R31R32, a0); gte_write_ctrl(CR_R33, a1);

  // WIDESCREEN, PART TWO — RE-CENTRING THE PROJECTION IS DELIBERATELY *NOT* DONE HERE, and the
  // reason is a measured architectural constraint rather than caution.
  //
  // Moving OFX to nw/2 works exactly as intended: measured, this renderer's content shifts +79px of
  // the expected +86 (correlating the sky band it owns), while content from renderers that are not
  // owned yet stays put (0px, ground band). That is the problem. A frame is drawn by SEVERAL of this
  // game's assembly renderers — muting this one removes the sky and distant terrain but leaves the
  // ground, characters and HUD — so shifting the projection in one of them MISALIGNS the scene
  // against itself: the plateau slides 86 columns off the ground it stands on, leaving visible seams.
  //
  // So INCREMENTAL OWNERSHIP DOES NOT PERMIT INCREMENTAL WIDESCREEN. The projection change is
  // all-or-nothing across every renderer that contributes to a frame, and until they are all owned the
  // honest state is the one below: widen the CLIP BOUNDS only. That never moves existing content — it
  // only stops faces being thrown away — so the frame stays self-consistent and simply extends to one
  // side. Asymmetric, but coherent, which is strictly better than centred and torn.
  //
  // The code to do it is three lines (read CR_OFX, write gpu_vk_wide_engine_ofx(c) << 16, restore at
  // exit) and was verified to work; it goes back in when the last contributing renderer is owned.

  uint32_t fp = c->mem_r32(kPoolPtr);
  uint32_t ra = kWorkList;
  uint32_t t8 = c->mem_r32(kPoolLimit);
  fp += 4;
  const uint32_t gp = fp;
  uint32_t sp = fp - 4;
  uint32_t t9 = 0;
  t8 -= 1024;

  uint32_t s0, s1, s2, s3, s4, s5, s6, s7;
  bool pool_out = false;

  for (;;) {
    s3 = c->mem_r32(ra);
    ra += 4;
    if (s3 == 0) break;
    at = c->mem_r32(s3 + 8);
    v0 = c->mem_r32(s3 + 12);
    s6 = (uint32_t)((int32_t)at >> 16);
    s5 = (uint32_t)((int32_t)(at << 16) >> 16);
    s4 = (uint32_t)((int32_t)v0 >> 16);
    s1 = s3 + 24;
    s2 = ((v0 & 0xFFFF) << 2) + s1 + 8;
    t4 = c->mem_r32(s3 + 16);
    s7 = kScratchpad;
    t5 = kClipTop; t6 = kClipBottom; t7 = kClipRight;
    // WIDESCREEN. This is what owning the renderer bought: the horizontal bounds are ours, so they
    // can move with the projection instead of being frozen as immediates in guest code. At 4:3 both
    // are exactly the guest's values, so the body stays byte-identical and the differential still
    // certifies it — the widening only exists when the user has asked for a wider aspect.
    int32_t wide_left = 0;
    if (gpu_vk_wide_engine(c)) {
      const int nw = gpu_vk_wide_engine_w(c);          // scales from the game's own 4:3 width
      const int margin = (nw - 512) / 2;               // split the extra width either side
      if (margin > 0) {
        t7 = (uint32_t)((512 + margin) << 16);         // right bound moves out
        wide_left = -margin;                           // and the left bound goes negative
      }
    }
    s0 = 0xFFFFFFFFu;

    // 4a. Vertex loop, software pipelined. One vertex is in flight in the GTE while the next is
    //     unpacked; the result read here belongs to the PREVIOUS RTPS.
    auto unpack = [&](uint32_t w, uint32_t& oz, uint32_t& oxy) {
      uint32_t z = w >> 21;
      uint32_t y = (w >> 10) & 0x7FF;
      uint32_t x = w & 0x3FF;
      oz = z + s4;
      oxy = (s5 - y) + ((s6 - x) << 16);
    };
    at = c->mem_r32(s1); s1 += 4;
    unpack(at, v0, v1);
    gte_write_data(DR_VZ0, v0);
    gte_write_data(DR_VXY0, v1);
    at = c->mem_r32(s1); s1 += 4;
    for (;;) {
      gte_op(c, kRTPS);
      unpack(at, v0, v1);
      at = c->mem_r32(s1);
      gte_write_data(DR_VZ0, v0);
      v0 = gte_read_data(DR_SXY2);          // the PREVIOUS vertex's projected screen XY
      gte_write_data(DR_VXY0, v1);
      a0 = v0 << 5;                          // make room for the clip code in the low bits
      a1 = v0 - t5;
      if (!((int32_t)a1 > 0)) a0 += 1;       // sy <= 0
      a1 = v0 - t6;
      if (!((int32_t)a1 < 0)) a0 += 2;       // sy >= 256
      a1 = v0 << 16;
      // The guest compares against zero; widescreen compares against a negative left edge instead.
      if (!((int32_t)a1 > (wide_left << 16))) a0 += 4;   // sx <= left bound
      a1 = a1 - t7;
      s1 += 4;
      if (!((int32_t)a1 < 0)) a0 += 8;       // sx >= 512  <- kClipRight
      s0 &= a0;
      c->mem_w32(s7, a0);
      s7 += 4;
      if (s1 == s2) break;
    }
    s0 &= 0xF;
    if (s0 != 0) continue;                   // every vertex off the same side: drop the object

    // 4c. Face loop.
    s7 = kScratchpad;
    s4 = s2 - 8;
    s5 = (t4 >> 14) + s4;
    s6 = ((t4 << 3) & 0xFFF8) + s5;
    s3 = kTagG3; s2 = kTagF3; s1 = kTagSub;
    for (;;) {
      t4 = c->mem_r32(s5);
      if (s5 == s6) break;
      s5 += 8;
      at = t8 - fp;
      // `srl t6, t4, 20` is this branch's delay slot too, so t6 is set even when the pool is
      // exhausted. Latent rather than observed — the pool did not run out in the verified runs — but
      // it is exit state all the same.
      if ((int32_t)at <= 0) { t6 = t4 >> 20; pool_out = true; break; }
      t6 = (t4 >> 20) + s7;
      t7 = ((t4 >> 10) & 0x3FC) + s7;
      s0 = (t4 & 0x3FC) + s7;
      at = c->mem_r32(t6);
      v0 = c->mem_r32(t7);
      v1 = c->mem_r32(s0);
      t5 = c->mem_r32(s5 - 4);
      // `xor a0, t9, fp` is the DELAY SLOT of the cull branch, so it runs even when the face is
      // rejected — a0 is clobbered either way. Skipping it on the cull path left a0 holding the clip
      // mask instead of the link word, which is the one register ndiff caught differing.
      const uint32_t cull = at & v0 & v1 & 0x1F;
      a0 = t9 ^ fp;
      if ((int32_t)cull > 0) continue;        // the three vertices share an off-screen side
      c->mem_w32(sp, a0);
      sp = fp;
      at = (uint32_t)((int32_t)at >> 5);     // unshift away the clip code
      v0 = (uint32_t)((int32_t)v0 >> 5);
      v1 = (uint32_t)((int32_t)v1 >> 5);
      c->mem_w32(fp + 8, at);                // speculative layout, overwritten on the F3 path
      c->mem_w32(fp + 16, v0);
      c->mem_w32(fp + 24, v1);
      t6 = t5 >> 20;
      t7 = (t5 >> 10) & 0x3FC;
      s0 = t5 & 0x3FC;
      if (t6 == t7 && t6 == s0) {            // one shared colour -> flat F3
        t6 = c->mem_r32(t6 + s4);
        t9 = s2;
        t6 -= s1;
        c->mem_w32(fp + 4, t6);
        c->mem_w32(fp + 8, at);
        c->mem_w32(fp + 12, v0);
        c->mem_w32(fp + 16, v1);
        fp += 20;
      } else {                               // three colours -> untextured Gouraud G3
        t6 = t6 + s4; t7 = t7 + s4; s0 = s0 + s4;
        t6 = c->mem_r32(t6); t7 = c->mem_r32(t7); s0 = c->mem_r32(s0);
        t9 = s3;
        c->mem_w32(fp + 4, t6);
        c->mem_w32(fp + 12, t7);
        c->mem_w32(fp + 20, s0);
        fp += 28;
      }
    }
    if (pool_out) break;
  }
  if (pool_out) c->mem_w32(kPoolOverflow, 1);

  // 5. Publish the pool pointer and link the batch into the ordering table.
  c->mem_w32(kPoolPtr, fp);
  if (fp != gp) {
    at = c->mem_r32(kOtBase);
    t9 ^= 0x80000000u;
    v0 = c->mem_r32(at + 16376);
    c->mem_w32(sp, t9);
    c->mem_w32(at + 16376, sp);
    if (v0 == 0) {
      c->mem_w32(at + 16380, gp);
    } else {
      v1 = gp >> 16;
      c->mem_w16(v0 + 0, (uint16_t)gp);
      c->mem_w8(v0 + 2, (uint8_t)v1);
    }
  }

  // Restore, exactly as the epilogue does.
  R[1] = kSaveArea;
  R[31] = c->mem_r32(kSaveArea + 44);
  R[30] = c->mem_r32(kSaveArea + 40);
  R[29] = c->mem_r32(kSaveArea + 36);
  R[28] = c->mem_r32(kSaveArea + 32);
  for (int i = 7; i >= 0; i--) R[16 + i] = c->mem_r32(kSaveArea + 4 * i);

  // Scratch registers the body leaves behind. ndiff compares all 31, so these are contract too.
  R[2] = v0; R[3] = v1; R[4] = a0; R[5] = a1;
  R[11] = t3; R[12] = t4; R[13] = t5; R[14] = t6; R[15] = t7; R[24] = t8; R[25] = t9;
}

void terrain_owned(Core* c) {
  if(!cfg_on("PSXPORT_TERRAIN_ORACLE")){
    ndiff_run(c,"terrain@0x8004EBA8",terrain_native,gen_func_8004EBA8);return;
  }
  TerrainOracleCapture capture{};
  capture.poolStart=c->mem_r32(kPoolPtr);capture.otBase=c->mem_r32(kOtBase);
  capture.oldTail=c->mem_r32(capture.otBase+16376u);capture.oldHead=c->mem_r32(capture.otBase+16380u);
  static constexpr uint32_t targets[]={0x8004ED44u,0x8004EE84u,0x8004EF68u,0x8004EF78u};
  if(!c->pcObserver.arm(targets,std::size(targets),terrain_checkpoint,&capture))abort();
  ndiff_run(c,"terrain@0x8004EBA8",terrain_native,gen_func_8004EBA8);
  const uint64_t seen=c->pcObserver.seen(),matched=c->pcObserver.matched();c->pcObserver.disarm();
  if(!seen||!matched||!capture.checkpoints){
    lucent::debug("terrainoracle","objects=0 candidates=0 clip_rejects=0 F3=0 G3=0 other_command=0 pool_rejection=0 emitted=0 bytes_compared=0 chain_compared=0 splice_compared=0 mismatched=0 max_faces=0 checkpoints={} seen={} matched={} corrupt_rejected=false first=checkpoint_not_reached result=NO_CORPUS (NDIFF budget exhausted or generated leg not executed)",capture.checkpoints,seen,matched);
    return;
  }
  const bool positive=terrain_compare_packets(c,capture)&&terrain_compare_chain(c,capture);
  bool negative=false;
  if(!capture.emitted.empty()){
    TerrainOracleCapture corrupt=capture;corrupt.mismatched=0;corrupt.first="none";
    corrupt.emitted[0].xy[0]^=1u;negative=!terrain_compare_packets(c,corrupt)&&corrupt.mismatched>0;
    TerrainOracleCapture brokenLink=capture;brokenLink.mismatched=0;brokenLink.first="none";
    brokenLink.emitted[0].pool^=4u;negative=negative&&!terrain_compare_chain(c,brokenLink)&&brokenLink.mismatched>0;
  }
  const bool corpus=!capture.emitted.empty();
  lucent::info("terrainoracle","objects={} candidates={} clip_rejects={} F3={} G3={} other_command={} pool_rejection={} emitted={} bytes_compared={} chain_compared={} splice_compared={} mismatched={} max_faces={} checkpoints={} seen={} matched={} corrupt_rejected={} first={} result={}",
    capture.objects,capture.candidates,capture.clipRejects,capture.f3,capture.g3,capture.other,
    capture.poolRejects,capture.emitted.size(),capture.bytesCompared,capture.chainCompared,capture.spliceCompared,capture.mismatched,capture.maxFaces,
    capture.checkpoints,seen,matched,negative,capture.first,corpus&&positive&&negative?"PASS":"FAIL");
  if(!corpus||!positive||!negative)abort();
}

}  // namespace

int spyro_native_terrain_selftest(){
  TerrainOracleCapture o{};constexpr uint32_t base=0x80001000u;
  TerrainObservedFace f{1,0,base,false,{0x00020001u,0x00040003u,0x00060005u},{0x20112233u,0x20112233u,0x20112233u}};
  TerrainObservedFace g{2,0,base+20u,true,{0x00120011u,0x00140013u,0x00160015u},{0x30112233u,0x30445566u,0x30778899u}};
  o.emitted={f,g};o.poolStart=base-4u;o.otBase=0x80002000u;
  std::vector<uint32_t> w{0x84001014u,f.rgb[0],f.xy[0],f.xy[1],f.xy[2],0x86000000u,g.rgb[0],g.xy[0],g.rgb[1],g.xy[1],g.rgb[2],g.xy[2]};
  auto read=[&](uint32_t a){if(a==o.otBase+16376u)return base+20u;if(a==o.otBase+16380u)return base;return w.at((a-base)/4u);};
  bool ok=terrain_compare_read(o,read)&&terrain_compare_chain_read(o,read)&&o.bytesCompared==48u&&o.mismatched==0;
  TerrainOracleCapture corrupt{};corrupt.emitted={f,g};w[9]^=1u;
  ok=ok&&!terrain_compare_read(corrupt,read)&&corrupt.mismatched>0&&!std::strcmp(corrupt.first,"xy");
  TerrainOracleCapture badLink{};badLink.emitted={f,g};badLink.otBase=o.otBase;w[0]^=1u;
  ok=ok&&!terrain_compare_chain_read(badLink,read)&&badLink.mismatched>0&&!std::strcmp(badLink.first,"packet_link");
  w[0]^=1u;
  TerrainOracleCapture existing{};existing.emitted={f,g};existing.poolStart=base-4u;
  existing.otBase=o.otBase;existing.oldTail=0x80003000u;existing.oldHead=0x80003100u;
  auto existingRead=[&](uint32_t a){if(a==existing.otBase+16376u)return base+20u;
    if(a==existing.otBase+16380u)return existing.oldHead;if(a==existing.oldTail)return 0x09001000u;
    return w.at((a-base)/4u);};
  ok=ok&&terrain_compare_chain_read(existing,existingRead);
  TerrainOracleCapture badHead=existing;badHead.mismatched=0;badHead.first="none";
  auto corruptHeadRead=[&](uint32_t a){if(a==badHead.otBase+16380u)return badHead.oldHead+4u;return existingRead(a);};
  ok=ok&&!terrain_compare_chain_read(badHead,corruptHeadRead)&&badHead.mismatched>0&&
    !std::strcmp(badHead.first,"ot_head_preserve");
  if(ok)lucent::info("selftest","PASS(terrainrecipe): F3=1 G3=1 bytes=48 corrupt_packet_rejected=1 corrupt_link_rejected=1 existing_head_preserved=1 corrupt_head_rejected=1");
  else lucent::error("selftest","FAIL(terrainrecipe): positive={} corrupt_mismatches={} first={}",o.mismatched,corrupt.mismatched,corrupt.first);
  return ok?0:1;
}

void spyro_register_native_terrain() {
  // OWNED. Verified byte-exact against the recompiled body over 400 consecutive calls — RAM,
  // scratchpad, all 31 GPRs and the COP2 register file — and re-verified on the first 8 calls of
  // every gate run (PSXPORT_NDIFF).
  //
  // ONE PATH REMAINS UNVERIFIED, stated because a green differential only covers what it executed:
  // the packet-pool-exhausted arm (0x8004EF68) did not fire in any verified run, so its exit state —
  // including the delay-slot `t6 = t4 >> 20` — is transcribed but unexercised. If the pool ever runs
  // out while ndiff has budget left, that call is where a divergence would surface.
  //
  // THE GATE IS EXPLICIT, AND IT USED TO BE AN ORDERING ACCIDENT. This registration was unconditional
  // while game_hooks.cpp and wide_clip.cpp both documented it as off unless PSXPORT_NATIVE_TERRAIN=1.
  // The behaviour happened to be right only because wide_clip.cpp registers LATER and claims the same
  // single override slot for 0x8004EBA8, silently displacing this one — the exact hazard game_hooks.cpp
  // records for fntrace. Net effect was correct and the mechanism was a lie, so nothing would have
  // caught it if the registration order ever changed. Now both sides test the same flag: wide_clip
  // skips this address when the flag is on, and this installs only when it is on.
  if (!cfg_on("PSXPORT_NATIVE_TERRAIN")) return;
  lucent::info("native", "PSXPORT_NATIVE_TERRAIN=1 — the native terrain body OWNS 0x8004EBA8; "
                         "wide_clip is standing down from this renderer, so widescreen's clip-bound "
                         "widening does NOT apply to it in this run.");
  psxport_recomp()->shard_set_override(0x8004EBA8u, terrain_owned);
}
