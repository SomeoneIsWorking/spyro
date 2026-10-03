#include "paired_actor_color_fade.h"

#include "gte_color_ops.h"

namespace spyro::paired_actor_color_fade {

bool active(uint32_t control) {
  return (control >> 24) != 0u;
}

void apply(uint32_t control, std::span<uint32_t> materials) {
  if (!active(control)) {
    return;
  }
  // The control word is one packed colour plus the strength that pulls toward it: the far colour in
  // bits 0..23, a byte per channel, and the interpolation factor in bits 24..31. Every field is
  // shifted left four on its way into the GTE, which is what makes a byte channel a Q4 one.
  const int32_t ir0 = (int32_t)((control >> 24) & 0xffu) << 4;
  const gte_color::Vector3 farColor{(int32_t)((control << 4) & 0xff0u),
                                    (int32_t)((control >> 4) & 0xff0u),
                                    (int32_t)((control >> 12) & 0xff0u)};
  for (uint32_t &entry : materials) {
    entry = gte_color::dpcs(entry, farColor, ir0);
  }
}

} // namespace spyro::paired_actor_color_fade
