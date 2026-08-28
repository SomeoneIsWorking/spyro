// Native ownership of libmcrd's asynchronous request starters:
// MemCardExist (0x8006635C) and MemCardAccept (0x800665B8).
//
// Both bodies are the same idle-or-busy transaction. The operation-specific callback is pushed
// onto the already-owned event stack, while the generated bodies remain the per-call NDIFF oracles.
#include "core.h"
#include "memcard_event_stack.h"
#include "memcard_operations.h"
#include "native_diff.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro_game.h"

namespace {

constexpr uint32_t kBusyMessage = 0x80011F54u;

void startMemcardOperationNative(Core *c, spyro::MemcardOperation operation) {
  c->r[29] -= 24u;
  c->r[3] = spyro::kMemcardPendingOperation;
  c->mem_w32(c->r[29] + 16u, c->r[31]);

  c->r[2] = c->mem_r32(c->r[3]);
  c->r[5] = c->r[4];
  if (c->r[2] != 0u) {
    c->r[4] = kBusyMessage;
    c->r[31] = operation == spyro::MemcardOperation::Exist ? 0x800663C4u : 0x80066620u;
    func_8006279C(c);
    c->r[2] = 0u;
  } else {
    const spyro::MemcardOperationPlan plan = spyro::memcardOperationPlan(operation);
    c->r[4] = plan.callback;
    c->mem_w32(c->r[3], static_cast<uint32_t>(plan.operation));
    c->mem_w32(spyro::kMemcardPhase, 0u);
    c->mem_w32(spyro::kMemcardResult, 0u);
    c->mem_w32(spyro::kMemcardRequestArgument, c->r[5]);
    c->r[31] = operation == spyro::MemcardOperation::Exist ? 0x800663ACu : 0x80066608u;
    func_80068F44(c);
    c->r[2] = 1u;
  }

  c->r[31] = c->mem_r32(c->r[29] + 16u);
  c->r[29] += 24u;
}

void memcardExistNative(Core *c) {
  startMemcardOperationNative(c, spyro::MemcardOperation::Exist);
}

void memcardAcceptNative(Core *c) {
  startMemcardOperationNative(c, spyro::MemcardOperation::Accept);
}

void memcardExistOwned(Core *c) {
  ndiff_run(c, "memcard-exist@0x8006635C", memcardExistNative, gen_func_8006635C);
}

void memcardAcceptOwned(Core *c) {
  ndiff_run(c, "memcard-accept@0x800665B8", memcardAcceptNative, gen_func_800665B8);
}

} // namespace

void spyro_register_native_memcard_operations() {
  psxport_recomp()->shard_set_override(0x8006635Cu, memcardExistOwned);
  psxport_recomp()->shard_set_override(0x800665B8u, memcardAcceptOwned);
}
