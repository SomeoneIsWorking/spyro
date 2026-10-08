// spyro2_hud_anchor.h — Spyro 2's in-level HUD producers, the anchor class of each, and the native
// overrides that apply `spyro::ui_anchor` to the screen X they emit.
//
// THE DRAWER. The HUD is not a moby pass: it is 2D textured sprites/quads emitted by one leaf,
// FUN_800520CC(image, x, y, size) (0x2C/0x2E quad or 0x64/0x66 sprite, linked by FUN_8001B390), and
// called for the HUD by FUN_80053E78 (the HUD pass of the object draw FUN_800155A0, bit 0x20):
//
//   80053F5C  jal 0x8005251C   counter(&gem   0x8006765C, image, x = 0x28,  y)   left edge
//   80053FA8  jal 0x8005251C   counter(&orb   0x80067664, image, x = 0x198, y)   right edge
//             jal 0x80052D84   meter: x = 0x200 - slide, the right-edge gauge     right edge
//             jal 0x80052B88   lives: head at x = 0xD0, digits from 0xF0          centred
//
// The guest authors every one of these against its 512-column frame, and the framework presents
// guest 2D centred in the widened frame (authored x + margin). So the per-class correction
// (`ui_anchor::correction`) is what each producer needs: left edge -margin, centred 0, right edge
// +margin. At 4:3 the correction is zero for every class, so both overrides are retail exactly.
//
// WHERE IT IS APPLIED: at the producer's x argument, before any packet exists. The counter's
// element pointer names its class, so its override moves its `x` argument; the meter computes its x
// internally, so its five emitter calls are recognised by their return address in the emitter
// override. No guest variable is written.
#pragma once

#include "ui_anchor.h"

#include <array>
#include <cstdint>
#include <optional>

class Core;

namespace spyro2::hud_anchor {

inline constexpr std::uint32_t kSpriteEmitter = 0x800520CCu;
inline constexpr std::uint32_t kCounterDrawer = 0x8005251Cu;

// The two counter elements FUN_80053E78 passes to the counter drawer (80053F48 / 80053F94).
inline constexpr std::uint32_t kGemCounter = 0x8006765Cu;
inline constexpr std::uint32_t kOrbCounter = 0x80067664u;

// The return addresses of the meter drawer FUN_80052D84's five `jal 0x800520CC`
// (80052E8C, 80052EA0, 80052EB4, 80052F0C, 80052F34).
inline constexpr std::array<std::uint32_t, 5> kMeterEmitterReturns = {
    0x80052E94u, 0x80052EA8u, 0x80052EBCu, 0x80052F14u, 0x80052F3Cu};

// The class of a counter element, or nullopt for an element this table does not know.
constexpr std::optional<spyro::ui_anchor::Anchor> counterAnchor(std::uint32_t element) {
  if (element == kGemCounter) {
    return spyro::ui_anchor::Anchor::LeftEdge;
  }
  if (element == kOrbCounter) {
    return spyro::ui_anchor::Anchor::RightEdge;
  }
  return std::nullopt;
}

// The class of one emitter call, from its return address: only the meter's calls are classed here
// (the counters are corrected at their own entry, every other caller is not HUD).
constexpr std::optional<spyro::ui_anchor::Anchor> emitterAnchor(std::uint32_t returnAddress) {
  for (const std::uint32_t meterReturn : kMeterEmitterReturns) {
    if (returnAddress == meterReturn) {
      return spyro::ui_anchor::Anchor::RightEdge;
    }
  }
  return std::nullopt;
}

// Install the counter and emitter overrides for this Core's resident SCUS_944.25 image.
void registerOverrides(Core &core);

} // namespace spyro2::hud_anchor
