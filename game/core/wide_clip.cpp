// wide_clip.cpp — widescreen, by moving the guest's own clip bounds instead of replacing its renderers.
//
// THE BLOCKER, AND WHY IT LOOKED LIKE A WALL. Every one of this game's hand-written assembly geometry
// renderers rejects faces against clip bounds that are IMMEDIATE constants in its own instruction
// stream: `lui rX, 0x0200` is sx >= 512, the 4:3 right edge. The static recompiler bakes those into C
// literals, so nothing downstream can move them, and a renderer whose bounds cannot move cannot draw
// the extra columns of a wider frame. From there the conclusion followed that widescreen needs every
// contributing renderer OWNED — reimplemented natively, byte-exact, ~9150 instructions of assembly
// across four functions (C133/C137) — and that re-centring the projection is all-or-nothing across
// them, so nothing is visible until the last one lands (C135).
//
// THE WAY THROUGH. Those immediates are only baked on the RECOMPILED path. They are still ordinary
// words in guest RAM, and the flat interpreter reads them from there — and interpreting these four
// renderers is BIT-IDENTICAL to running their recompiled bodies, verified per call against 2 MB of
// RAM, the scratchpad, every GPR and the whole COP2 register file (C139). So a widened bound is a
// one-word write with the game's own code still doing the drawing.
//
// THIS IS NOT A MAGIC CONSTANT. Each site below was READ before it was listed: the classifier that
// merely looked for `lui rX, 0x0200` was wrong once already (C133 — 0x800580F4's is a packet tag, not
// a bound), and six more tag sites inside 0x800258F0 alone would have been mis-patched by it. A site
// qualifies only when the surrounding code is the clip-code idiom — the 0x00010000 / 0x01000000 /
// 0x02000000 triple loaded together, then consumed by `sub` against the packed screen word to set
// bits 1/2/4/8. Anything else with the same immediate is a GPU command word.
//
// 4:3 IS BYTE-FOR-BYTE UNTOUCHED. The patch is applied and REVERTED around each call, so a vanilla
// run executes the original word; the aspect can also be toggled live from the F1 overlay, which is
// why this is decided per call rather than once at boot. Under PSXPORT_ORACLE the widescreen engine
// reports off, so the differential reference never sees a patched word.
#include "core.h"
#include "recomp_iface.h"
#include "cfg.h"      // cfg_on — PSXPORT_NATIVE_TERRAIN is a feature flag, not a diagnostic
#include "spyro_game.h"
#include <lucent/log.h>

void interp_call(Core* c, uint32_t pc);      // interp.cpp — nested call leaving the guest's ra alone
int  gpu_vk_wide_engine(Core*);              // a wider aspect is selected (and this is not the oracle)
int  gpu_vk_wide_engine_w(Core*);            // the wide native width, scaled from the game's own 512

namespace {

// A renderer, and every clip-bound immediate inside it. `sites` are the addresses of the `lui`
// instructions; `consumer` is where the loaded register is used as a bound, which is what makes the
// site a bound rather than a tag — cited so the next reader can check the classification rather than
// trust it.
struct Renderer {
  uint32_t addr;
  const char* what;          // what it draws — mute map C147, names from open-spyro
  uint32_t sites[8];
  uint32_t consumer;
  uint32_t ofx_site;         // an `lui rX, 0x0100` this body uses to RESET OFX to the 4:3 centre
};

// COP2 control register 24 — the projection's horizontal centre, 16.16 fixed point. 4:3 puts it at
// 256 (0x01000000); a 684-wide frame wants 342.
constexpr uint32_t CR_OFX = 24;

// The RIGHT-edge bound only. The left edge is an implicit compare against zero with no constant, so
// it cannot be moved the same way — which is fine, because the extra width is placed on both sides by
// re-centring the projection, not by moving the left bound out.
const Renderer kRenderers[] = {
  // 0x8004EBA8 IS in this table even though native_terrain.cpp reimplements it, because that
  // reimplementation is a bring-up switched off by default (PSXPORT_NATIVE_TERRAIN=1) — so on a
  // normal run this renderer's bound and projection come from the recompiled body like every other.
  // Leaving it out was visible immediately: the ground and the characters moved by the margin and
  // the SKY did not, which is exactly the self-misalignment C135 warns about, produced by omission
  // rather than by design. spyro_register_wide_clip() skips this entry when the native body is on.
  { 0x8004EBA8u, "EmitStaticActorMeshList — sky + distant terrain",
    { 0x8004ED8Cu }, 0x8004EE2Cu, 0 },                    // lui t7,0x0200 -> sub a1,a1,t7
  { 0x80022A2Cu, "RasterizeSpritePrimQueue — the foreground gem + the DEMO MODE caption",
    { 0x80022FF8u }, 0x800230ACu,                         // lui s0,0x0200 -> sub a0,a0,s0
    0x80023958u },   // `lui at,0x0100; ctc2 at,OFX` at 0x8002395C — this body RESETS the projection
                     // centre to the 4:3 256 partway through, so the OFX we install below would be
                     // silently undone for everything it draws after that point.
  { 0x8001F798u, "EmitActorDrawList — the orange character",
    { 0x8001F9F4u }, 0x8001FC94u, 0 },                    // lui t9,0x0200 -> sub t6,t6,t9
  { 0x80020F34u, "EmitSecondaryActorPrimitives — a second character",
    { 0x80021190u }, 0x80021454u, 0 },                    // lui t9,0x0200 -> sub t6,t6,t9
  { 0x800258F0u, "RenderWorldChunks — the GROUND and the cliffs",
    { 0x8002626Cu, 0x8002684Cu, 0x80027A60u, 0x8002817Cu,
      0x80028C50u, 0x800299ACu, 0x8002A2F4u, 0x8002A4C0u }, 0, 0 },
    // eight bounds, each the third of a 0x0001/0x0100/0x0200 triple. The SIX other 0x02000000
    // immediates in this function (0x8002652C, 0x800266E0, 0x80027924, 0x80028074, 0x80028A40,
    // 0x8002982C) are each `lui rX,0x3n00; lui rY,0x0200; add` — GPU command words, not bounds.
};
constexpr int kRendererCount = (int)(sizeof kRenderers / sizeof kRenderers[0]);

// The unpatched instruction word for each site, captured the first time it is patched. Read from
// guest RAM rather than assumed, so a wrong address shows up as a refusal below instead of as a
// corrupted instruction.
uint32_t s_orig[kRendererCount][8];
bool s_have_orig[kRendererCount][8];
bool s_refused[kRendererCount];
bool s_said[kRendererCount];
bool s_said_43[kRendererCount];
uint32_t s_ofx_orig[kRendererCount];
bool s_have_ofx[kRendererCount];

// The right bound lives in the HIGH half of the packed screen word, so the immediate is simply the
// width: 512 -> 0x0200, 684 -> 0x02AC. Both are exact because the low 16 bits of `width << 16` are
// always zero — a `lui` can express any bound this game could want.
inline uint32_t with_bound(uint32_t insn, int width) {
  return (insn & 0xFFFF0000u) | (uint32_t)(width & 0xFFFF);
}

// ── THE INSTRUMENT (PSXPORT_DEBUG=wideprims), and why it is this quantity and not a picture.
//
// "Did widening the bound change anything?" was asked of a screenshot first, and a screenshot could
// not answer it: the frames differed by FIVE pixels, and a column-correlation search between the 4:3
// and 16:9 captures reported its own SEARCH BOUND as the best match in every band — this scene is
// mostly flat sand and does not discriminate. A metric that cannot separate the two answers is not
// evidence either way.
//
// The quantity that actually moves is upstream of the pixels: how much geometry the renderer EMITS.
// Every one of these bodies appends packets to the pool at 0x800757B0 and advances that pointer, so
// the bytes it wrote in a call is (pointer after - pointer before) — no packet-format knowledge
// needed, and no dependence on whether the result happens to be visible against a sandy background.
//
// MEASURED IN BOTH MODES, deliberately. The vanilla path reports too, so 4:3 and wide numbers come
// from the same counter and are directly comparable; an instrument that only runs in the mode you
// hope to confirm cannot show you the other answer. A renderer that emits ZERO bytes reports that
// explicitly with both pointers, so "drew nothing" never looks like "was never called".
constexpr uint32_t kPoolPtr = 0x800757B0u;   // the packet-pool write pointer (see native_terrain.cpp)

struct Emit { uint64_t calls, bytes; uint32_t zero_calls; };
Emit s_emit[8][2];                            // [renderer][0 = 4:3, 1 = wide]

void emit_report(Core* c, int ri, int wide, uint32_t before, uint32_t after) {
  // The ONE legitimate guard: it fences the per-call ACCUMULATION (counters, byte totals), which is
  // real state the logger cannot defer. The print below is unguarded — `debug` is the audience this
  // report always had, and it gates itself on the same channel.
  if (!lucent::channel_on("wideprims")) return;
  Emit& e = s_emit[ri][wide];
  e.calls++;
  if (after >= before) e.bytes += (after - before);
  if (after == before) e.zero_calls++;
  if (e.calls % 256) return;
  const Emit& other = s_emit[ri][wide ^ 1];
  lucent::debug("wideprims",
                "0x{:08X} {:<6} calls={} bytes/call={} (zero-emit calls={}, pool {:08X}->{:08X}). "
                "Same renderer in the other aspect so far: calls={} bytes/call={}.",
                kRenderers[ri].addr, wide ? "WIDE" : "4:3",
                e.calls, e.bytes / e.calls, e.zero_calls,
                before, after,
                other.calls, other.calls ? other.bytes / other.calls : 0);
}

void run(Core* c, int ri) {
  const Renderer& R = kRenderers[ri];
  const RecompRegistry* reg = psxport_recomp();
  const uint32_t pool_before = c->mem_r32(kPoolPtr);

  if (!gpu_vk_wide_engine(c) || s_refused[ri]) {
    // SAY SO, once. "No widening happened" has two completely different causes — the renderer was
    // never called, or it was called and the engine reports 4:3 — and without this line they look
    // identical in the log, which is the failure this file's own comment warns about.
    if (!s_said_43[ri]) {
      s_said_43[ri] = true;
      // R.what is a string literal in every kRenderers entry — never a null const char*.
      lucent::info("wide", "0x{:08X} ({}) called, but NOT widened: wide_engine={} refused={} — "
                           "running the recompiled body unchanged.",
                   R.addr, R.what, gpu_vk_wide_engine(c), (int)s_refused[ri]);
    }
    // Vanilla: run the recompiled body, exactly as if this file did not exist. The override is
    // cleared for the duration so the dispatch reaches the real body, and re-armed by the caller.
    reg->shard_set_override(R.addr, nullptr);
    reg->main_dispatch(c, R.addr);
    emit_report(c, ri, 0, pool_before, c->mem_r32(kPoolPtr));
    return;
  }

  const int nw = gpu_vk_wide_engine_w(c);
  int n = 0;
  for (; n < 8 && R.sites[n]; n++) {
    const uint32_t at = R.sites[n];
    if (!s_have_orig[ri][n]) {
      const uint32_t w = c->mem_r32(at);
      // A site that is not `lui rX, 0x0200` is not the instruction this table describes. Refuse the
      // whole renderer rather than patch a word whose meaning is unknown — and say so, because a
      // silently-skipped patch would present as "widescreen just does not widen here".
      // opcode(31:26)=lui, rs(25:21)=0, imm(15:0)=0x0200. The DESTINATION register (20:16) is free —
      // it differs per renderer (s0 in one, t9 in two, t3/t4/a1 across the eight in 0x800258F0), and
      // pinning it is how the first version of this check refused all eleven genuine sites.
      if ((w & 0xFFE0FFFFu) != 0x3C000200u) {
        lucent::error("wide", "0x{:08X}: expected `lui rX,0x0200` at the clip-bound site 0x{:08X}, "
                              "found {:08X}. NOT patching this renderer ({}) — its bounds stay at 4:3.",
                      R.addr, at, w, R.what);
        s_refused[ri] = true;
        break;
      }
      s_orig[ri][n] = w;
      s_have_orig[ri][n] = true;
    }
    c->mem_w32(at, with_bound(s_orig[ri][n], nw));
  }
  // The in-body OFX reset, where one exists: `lui rX, 0x0100` feeding ctc2 OFX. Same shape of patch,
  // same shape of refusal — 256 becomes nw/2, so the body re-centres to the WIDE centre rather than
  // undoing what was installed around the call.
  if (!s_refused[ri] && R.ofx_site) {
    if (!s_have_ofx[ri]) {
      const uint32_t w = c->mem_r32(R.ofx_site);
      if ((w & 0xFFE0FFFFu) != 0x3C000100u) {
        lucent::error("wide", "0x{:08X}: expected `lui rX,0x0100` at the OFX reset site 0x{:08X}, "
                              "found {:08X}. NOT widening this renderer ({}).",
                      R.addr, R.ofx_site, w, R.what);
        s_refused[ri] = true;
      } else {
        s_ofx_orig[ri] = w;
        s_have_ofx[ri] = true;
      }
    }
    if (s_have_ofx[ri])
      c->mem_w32(R.ofx_site, (s_ofx_orig[ri] & 0xFFFF0000u) | (uint32_t)(((nw / 2) << 16) >> 16));
  }

  if (s_refused[ri]) {
    for (int i = 0; i < n; i++) if (s_have_orig[ri][i]) c->mem_w32(R.sites[i], s_orig[ri][i]);
    run(c, ri);
    return;
  }

  // Say it ONCE per renderer, with the before and after word. A widening that silently does not fire
  // is indistinguishable from one that fires and changes nothing — and this project has recorded a
  // "no effect" non-result that was really a measurement fault more than once (issue 0039).
  if (!s_said[ri]) {
    s_said[ri] = true;
    lucent::info("wide", "0x{:08X} ({}): {} bound site(s) widened 512 -> {}, first at 0x{:08X} "
                         "{:08X} -> {:08X}; running interpreted",
                 R.addr, R.what, n, nw, R.sites[0], s_orig[ri][0], with_bound(s_orig[ri][0], nw));
  }

  // RE-CENTRE THE PROJECTION. Widening the bounds alone is very nearly a no-op — measured, it moved
  // FIVE pixels of a 684x240 frame (C140) — because the guest rejects a face only when all three of
  // its vertices are outside, and with the projection still centred on 256 almost nothing lies wholly
  // beyond 512. The two halves only work together: OFX at nw/2 pushes the world right by the margin,
  // which brings faces that were wholly off the LEFT edge into view (the left bound is a compare
  // against zero, so they stop being rejected on their own), and the widened right bound is what
  // keeps the faces that move PAST 512 from being rejected in exchange.
  //
  // Set and restored around the call, and applied to EVERY contributing renderer in the same frame.
  // That is what makes it safe now and unsafe before: shifting one renderer's projection while the
  // others keep theirs slides its content off the geometry it belongs to, with visible seams (C135).
  const uint32_t saved_ofx = gte_read_ctrl(CR_OFX);
  gte_write_ctrl(CR_OFX, (uint32_t)((nw / 2) << 16));
  interp_call(c, R.addr);
  gte_write_ctrl(CR_OFX, saved_ofx);
  emit_report(c, ri, 1, pool_before, c->mem_r32(kPoolPtr));

  // Put the guest's own instructions back. A bound must not stay widened for any other reader of this
  // code — and leaving it patched would make a later 4:3 toggle silently wrong.
  for (int i = 0; i < n; i++) c->mem_w32(R.sites[i], s_orig[ri][i]);
  if (R.ofx_site) c->mem_w32(R.ofx_site, s_ofx_orig[ri]);
}

template <int I> void hook(Core* c) {
  run(c, I);
  psxport_recomp()->shard_set_override(kRenderers[I].addr, hook<I>);
}

}  // namespace

void spyro_register_wide_clip() {
  // The native terrain body, when it is switched on, owns 0x8004EBA8 outright and names its own
  // bound — installing over it here would silently displace the very thing that bring-up is testing.
  if (!cfg_on("PSXPORT_NATIVE_TERRAIN")) psxport_recomp()->shard_set_override(kRenderers[0].addr, hook<0>);
  psxport_recomp()->shard_set_override(kRenderers[1].addr, hook<1>);
  psxport_recomp()->shard_set_override(kRenderers[2].addr, hook<2>);
  psxport_recomp()->shard_set_override(kRenderers[3].addr, hook<3>);
  psxport_recomp()->shard_set_override(kRenderers[4].addr, hook<4>);
  static_assert(kRendererCount == 5, "add a hook<> instantiation for the new renderer");
  lucent::info("wide", "clip-bound widening armed for {} renderers (11 sites). At 4:3 they run their "
                       "recompiled bodies untouched; at a wider aspect they run interpreted with the "
                       "right bound moved to the wide width.", kRendererCount);
}
