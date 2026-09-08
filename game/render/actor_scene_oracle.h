#pragma once

#include <cstdint>
#include <span>

class Core;

namespace spyro::actor_scene_oracle {

// DIAGNOSTIC ONLY, armed by PSXPORT_ACTOR_SCENE_ORACLE=1; otherwise every call is a no-op.
//
// Runs `retailBody` over the frame state the native producer has just consumed and decodes every
// GPU packet it links into the world OT, then prints that stream beside the native queue items
// carrying any of `nativePainterKeys`. One retail body routinely corresponds to several native
// painter keys: retail's moby-chain walker covers work the port splits across its regular,
// secondary and shaded actor producers, and comparing against one of them would report the other
// two as missing geometry. Both sides are printed with their denominators, so
// "no difference" can be told apart from "nothing was captured".
//
// It restores the ordering table and packet-pool cursor afterwards, but it does NOT undo whatever
// else the retail body wrote to guest state, which is why it may never run on a shipping frame.
void compare(Core *core,
             uint32_t retailBody,
             std::span<const uint32_t> nativePainterKeys,
             const char *site);

} // namespace spyro::actor_scene_oracle
