#include "native_pixel_fade.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// One channel of the fade: the byte plus the caller's amount, dropped to zero when the sum is not
// positive. The guest branches signed (`blez`) on a 32-bit sum, so a large amount wraps rather than
// saturating.
std::uint32_t fadedChannel(std::uint32_t value, std::uint32_t fade, std::uint32_t shift) {
  const std::uint32_t faded = value + fade;
  if (static_cast<std::int32_t>(faded) <= 0) {
    return 0u;
  }
  return faded << shift;
}

// ── 0x80017F24 — subtract a per-channel fade amount from every pixel of a run list.
//     at = [a0] ; t8 = a0+4 ; v0 = [t8] ; t8 = a0+8
//     bltz at,EXIT ; sll v1,at,0xf (delay slot)
//     srl v1,v1,0xd ; t9 = a1 + v1          descriptor bits 2..16 = destination byte offset,
//     at = (at>>17)<<2 ; at += t9           bits 15..31 = run length in bytes, and the sign bit of
//     <per pixel>                           the length is the end-of-list marker.
//     EXIT: jr ra ; v1 = at<<15, v0 = the word one past the terminating descriptor
// The source is a list of (descriptor, pixels) pairs and the destination is a flat pixel buffer the
// descriptors address sparsely, so one call fades a scattered region rather than a rectangle.
//
// Two delay slots decide the exit state. `addi a3,zero,0` clears the accumulator on BOTH arms of
// the first channel test, so a clamped channel contributes nothing instead of inheriting the pixel
// before it; `addi t9,t9,4` advances the destination on BOTH arms of the alpha test, so a clamped
// alpha still consumes its slot and the run's word count stays exact.
//
// v0 leaves holding the word read one PAST the terminating descriptor — the prefetch at the top of
// the loop always runs before the sign test — and v1 the terminating descriptor shifted left 15,
// the bltz delay slot, never the offset it computed on the way in.
void writeFadedPixelRuns(Core *c) {
  std::uint32_t destination = c->r[5];
  const std::uint32_t fade = c->r[6];
  std::uint32_t descriptor = c->mem_r32(c->r[4]);
  std::uint32_t pixel = c->mem_r32(c->r[4] + 4);
  std::uint32_t cursor = c->r[4] + 8;
  for (;;) {
    if (static_cast<std::int32_t>(descriptor) < 0) {
      c->r[2] = pixel;
      c->r[3] = descriptor << 15;
      return;
    }
    destination += (descriptor << 15) >> 13;
    const std::uint32_t runEnd = destination + ((descriptor >> 17) << 2);
    do {
      const std::uint32_t blue = (pixel >> 24) & 0xffu;
      const std::uint32_t green = (pixel >> 16) & 0xffu;
      const std::uint32_t red = (pixel >> 8) & 0xffu;
      const std::uint32_t alpha = pixel & 0xffu;
      pixel = c->mem_r32(cursor);
      std::uint32_t word = fadedChannel(blue, fade, 24);
      word += fadedChannel(green, fade, 16);
      word += fadedChannel(red, fade, 8);
      word += fadedChannel(alpha, fade, 0);
      destination += 4;
      c->mem_w32(destination - 4, word);
      cursor += 4;
    } while (destination != runEnd);
    descriptor = pixel;
    pixel = c->mem_r32(cursor);
    cursor += 4;
  }
}

} // namespace

void registerPixelFadeOverrides(Core &core) {
  psx::cpu::installNativeOverride(core, 0x80017F24u, "fade_pixel_runs", writeFadedPixelRuns);
}

} // namespace spyro1::native
