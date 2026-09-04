// native_gameplay.cpp — the first PC-owned gameplay correction.
//
// The guest's digital movement routine reads the buffered pad's m_Released field when selecting a
// direction. The guest pad producer defines that field as buttons released this frame, while the
// direction must remain active for every held frame. The live route proves the consequence: the
// active pad contains m_Held=0x8000 for Left and m_Released=0, but Spyro's position does not move.
//
// This is a narrow runtime override. It runs the ordinary guest body first,
// then supplies the missing held-direction target-speed update. Analog input and release-edge
// behavior remain owned by the runtime guest path. The native ownership stays confined to the
// diagnosed contract defect.
#include "native_gameplay.h"

#include "core.h"
#include "native_execution.h"
#include "spyro_game.h"

namespace {

constexpr std::uint32_t kDigitalMovement = 0x8003D3B8u;
constexpr std::uint32_t kActivePad = 0x800757E0u;
constexpr std::uint32_t kDirectionTable = 0x8006C5D0u;
constexpr std::uint32_t kCamera = 0x80076DD0u;
constexpr std::uint32_t kSpyro = 0x80078A58u;
constexpr std::uint32_t kTargetSpeed = kSpyro + 0xC8u;
constexpr std::uint32_t kTargetRotationZ = kSpyro + 0xCCu;

// Gamepad.m_BufferedInputs layout, verified against external/spyro-1/include/gamepad.h:
// type +0, held +4, down +8, released +12, left-stick-moved +16, sticks +20.
constexpr std::uint32_t kPadHeld = 0x04u;
constexpr std::uint32_t kPadLeftStickMoved = 0x10u;
constexpr std::uint32_t kPadLeftX = 0x16u;
constexpr std::uint32_t kPadLeftY = 0x17u;

void digitalMovementOwned(Core *c) {
  const std::int32_t speed = static_cast<std::int32_t>(c->r[4]);
  if (!spyro::callOriginalOrPropagate(*c, kDigitalMovement)) {
    return;
  }

  const std::uint32_t activePad = c->mem_r32(kActivePad);
  if (activePad == 0) {
    return;
  }

  // The guest body is authoritative whenever a moved analog stick is active. The native
  // correction only fills the digital branch that is otherwise keyed off the release edge.
  const bool analogMoved = c->mem_r32(activePad + kPadLeftStickMoved) != 0;
  const bool analogIsDeflected =
      c->mem_r8(activePad + kPadLeftX) != 0x7Fu || c->mem_r8(activePad + kPadLeftY) != 0x7Fu;
  if (analogMoved && analogIsDeflected) {
    return;
  }

  const std::uint32_t held = c->mem_r32(activePad + kPadHeld);
  if (!spyro::gameplay::hasDigitalDirection(held)) {
    return;
  }

  const unsigned tableIndex = spyro::gameplay::digitalDirectionTableIndex(held);
  const std::int32_t direction =
      static_cast<std::int16_t>(c->mem_r16(kDirectionTable + tableIndex * 2u));
  const std::int32_t cameraRotation = static_cast<std::int16_t>(c->mem_r16(kCamera + 0x50u));
  c->mem_w32(kTargetSpeed, static_cast<std::uint32_t>(speed));
  c->mem_w32(kTargetRotationZ, static_cast<std::uint32_t>((direction + cameraRotation) & 0xFFF));
}

} // namespace

void spyro_register_native_gameplay(Core &core) {
  spyro::installNativeOverride(
      core, kDigitalMovement, "Spyro::DigitalMovement", digitalMovementOwned);
}
