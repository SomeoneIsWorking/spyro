#pragma once

#include <cstdint>
#include <vector>

// moby_shadow_list — the shadow list the three Moby passes build and 0x80059F8C consumes.
//
// 0x8001F158 resets the cursor at g_MobyShadows+8 to the list start every call, then it,
// 0x800208FC and 0x80022A2C append (Moby, model shadow-radius byte) pairs, and the Moby shadow
// renderer walks the list up to the cursor. Only those passes touch the cursor (external/spyro-1:
// every g_MobyShadows reference is in r_moby.s, r_shadows.s, or loaders.c's UV setup).
//
// The list is guest state, so it holds retail's 4:3 answer. The port's draw needs the widened
// answer as well, so every pass also produces a `List` of the entries it would stage under the
// drawn plane, and that list — never the guest one — is what the shadow producer draws (issue
// 0152).
namespace spyro::moby_shadow_list {

inline constexpr uint32_t kStart = 0x800724F4u;   // D_8006FCF4 + 0x2800
inline constexpr uint32_t kShadows = 0x80075EF8u; // g_MobyShadows: +0/+4 UVs, +8 the cursor
inline constexpr uint32_t kCursor = kShadows + 8u;

struct Entry {
  uint32_t moby = 0;
  uint32_t radius = 0; // the model's shadow byte; 0x80059F8C uses it as the fan radius
};

using List = std::vector<Entry>;

} // namespace spyro::moby_shadow_list
