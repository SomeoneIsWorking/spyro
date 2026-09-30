#include "native_spu_voice_attributes.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>
#include <string_view>

namespace spyro1::native {
namespace {

// The library's own voice-attribute field writer, the target of the body's only `jal`. It bounds
// both index arguments against 0x18, so 0x17 = 23 is the last field index it accepts.
constexpr std::uint32_t kVoiceAttrFieldWriter = 0x8005CFECu; // `jal` at 0x8005C7BC
constexpr std::uint32_t kFirstAttrField = 0u;
constexpr std::uint32_t kLastAttrField = 0x17u;

constexpr std::string_view kOverrideName = "spu_set_voice_attr";

// ── 0x8005C7AC (SpuSetVoiceAttr) — write the caller's voice-attribute block, a0, into the SPU by
//     handing it to the library's field writer with the field range fixed at 0..0x17 and a3 zero.
//     Retail writes no register but a1, a2 and a3 before the `jal`, so a0 and every other register
//     the callee reads are the caller's; v0 and v1 exit as the callee left them, the only result
//     this void entry produces. Its own 0x18-byte frame holds nothing but the saved $ra, so the
//     override opens none and the callee's frame stays inside the dead stack below sp that the
//     differential ignores.
void applyVoiceAttributes(Core *c) {
  psx::cpu::callGuestNow(
      *c, kOverrideName, kVoiceAttrFieldWriter, c->r[4], kFirstAttrField, kLastAttrField, 0u);
}

} // namespace

void registerSpuVoiceAttributeOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x8005C7ACu, "spu_set_voice_attr", applyVoiceAttributes);
}

} // namespace spyro1::native