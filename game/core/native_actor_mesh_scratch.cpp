// Native ownership of InitActorMeshScratchRegions (0x8005B6F8).
//
// Ground truth is SCUS_942.28 0x8005B6F8..0x8005B7D7 (56 instructions), independently named by
// the byte-matching open-spyro disassembly. The only child is FillWord 0x80016914, already owned in
// native_leaf.cpp; keeping that call boundary makes this the next bottom-up non-leaf ownership
// step. The generated body remains compiled and ndiff_run compares the reached boot call against
// it.
#include "actor_mesh_scratch.h"
#include "core.h"
#include "native_diff.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro_game.h"

namespace {

constexpr uint32_t kWorkAreaTop = 0x800785FCu;
constexpr uint32_t kEmitList = 0x800785F8u;
constexpr uint32_t kScratchOtBase = 0x800785F4u;
constexpr uint32_t kScratchPrimTop = 0x800785F0u;
constexpr uint32_t kScratchPrimBase1 = 0x800785ECu;
constexpr uint32_t kScratchRegionBase = 0x800785E8u;

constexpr uint32_t kFramePrimBase0 = 0x80076F50u;
constexpr uint32_t kFramePrimBase1 = 0x80076FD4u;
constexpr uint32_t kFrameMergedSlot0 = 0x80076F58u;
constexpr uint32_t kFrameMergedSlot1 = 0x80076FDCu;
constexpr uint32_t kFrameDepthBase0 = 0x80076F54u;
constexpr uint32_t kFrameDepthBase1 = 0x80076FD8u;

void fillWords(Core *c, uint32_t dst, uint32_t bytes, uint32_t returnPc) {
  c->r[4] = dst;
  c->r[5] = 0;
  c->r[6] = bytes;
  c->r[31] = returnPc;
  func_80016914(c);
}

void initActorMeshScratchNative(Core *c) {
  c->r[29] -= 24;
  c->mem_w32(c->r[29] + 16, c->r[31]);

  const spyro::ActorMeshScratchLayout layout =
      spyro::actorMeshScratchLayout(c->mem_r32(kWorkAreaTop), c->r[4] != 0);
  c->mem_w32(kEmitList, layout.emitList);
  c->mem_w32(kScratchOtBase, layout.otBase);
  c->mem_w32(kScratchPrimTop, layout.primTop);
  c->mem_w32(kScratchPrimBase1, layout.primBase1);
  c->mem_w32(kScratchRegionBase, layout.regionBase);

  // Preserve the executable's final scratch-register values as well as its published aliases.
  c->r[3] = c->mem_r32(kScratchRegionBase);
  c->r[7] = c->mem_r32(kScratchPrimBase1);
  c->r[4] = c->mem_r32(kScratchPrimTop);
  c->r[2] = c->mem_r32(kScratchOtBase);
  c->r[1] = 0x80070000u;
  c->mem_w32(kFramePrimBase0, c->r[3]);
  c->mem_w32(kFramePrimBase1, c->r[7]);
  c->mem_w32(kFrameMergedSlot0, c->r[4]);
  c->mem_w32(kFrameMergedSlot1, c->r[4]);
  c->mem_w32(kFrameDepthBase0, c->r[2]);
  c->mem_w32(kFrameDepthBase1, c->r[2]);

  fillWords(c, c->r[4], 8, 0x8005B7B4u);
  fillWords(c, c->mem_r32(kScratchOtBase), 0x4000, 0x8005B7C8u);

  c->r[31] = c->mem_r32(c->r[29] + 16);
  c->r[29] += 24;
}

void initActorMeshScratchOwned(Core *c) {
  ndiff_run(c, "actor-scratch@0x8005B6F8", initActorMeshScratchNative, gen_func_8005B6F8);
}

} // namespace

void spyro_register_native_actor_mesh_scratch() {
  psxport_recomp()->shard_set_override(0x8005B6F8u, initActorMeshScratchOwned);
}
