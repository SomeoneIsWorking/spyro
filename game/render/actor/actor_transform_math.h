#pragma once

#include "native_projection.h"

#include <array>
#include <cstdint>

class Core;

namespace spyro::actor_transform_math {

struct Matrix {
  std::array<std::array<int16_t, 3>, 3> value{};
};

// The GTE's five packed rotation words, in the R11R12/R13R21/R22R23/R31R32/R33 order every guest
// matrix in this title is stored in.
Matrix unpackMatrix(std::array<uint32_t, 5> words);
Matrix readCameraMatrix(Core *core);
Matrix readMatrix(Core *core, uint32_t address);
std::array<int32_t, 3> cameraRelativePosition(Core *core, uint32_t moby);
std::array<int32_t, 3> transform(const Matrix &matrix, std::array<int32_t, 3> vector);
// Which pair of matrix columns a rotation replaces. The sine/cosine pair is read at a BYTE offset
// into the shared table, because callers arrive at that offset by different scalings: the Moby
// helper below derives it from packed angle bytes, while the Moby-shadow renderer 0x80059F8C scales
// its two six-bit plane angles by eight.
enum class Axis : uint8_t { X, Y, Z };

// The shared 256-entry table at 0x8006CBF8, cosine one quarter turn later at +0x80. Callers reach
// their entry at a BYTE offset because they scale their angles differently.
struct SineCosine {
  int16_t sine = 0;
  int16_t cosine = 0;
};
SineCosine sineCosine(Core *core, uint32_t tableByteOffset);

Matrix rotateAxis(Core *core, Matrix matrix, Axis axis, uint32_t tableByteOffset);

Matrix rotateForMoby(Core *core, Matrix matrix, uint32_t packedAngles);
std::array<uint32_t, 5> packMatrix(const Matrix &matrix, int16_t cr30);

psxport::native_projection::FixedAffine
worldAffine(Core *core, uint32_t moby, const Matrix &camera, std::array<int32_t, 3> &view);

// A Moby whose render-radius byte (+0x50) has bit 7 set is a SCREEN-SPACE Moby, and 0x80022A2C
// branches to 0x80022D1C for it before any culling (`sll $a0,$a0,24; bltz $a0` at 0x80022B34 and
// 0x80022B38). Every `g_Hud` Moby and every HUD glyph carries 0xFF there. The two helpers below are
// that path's own view, the only thing it differs from the world path in for the geometry.
constexpr bool isScreenSpace(uint8_t renderRadius) {
  return (renderRadius & 0x80u) != 0u;
}

// 0x80022D64-0x80022D88: the GTE rotation starts as diag(0x1000, 0xA00, 0x1000) instead of the
// camera matrix, then takes the Moby's own rotation through the same steps as a world Moby.
// 0x80022D3C (`sra $v1, $v1, 1`) and 0x80022DC4 make the translation (0, 0, z >> 1) from the
// record's own position.z, with no scale byte and no camera. Only the translation's Z is nonzero.
psxport::native_projection::FixedAffine screenSpaceAffine(Core *core, uint32_t moby);

// 0x80022D1C-0x80022D30: the screen-space path writes the Moby's own position.x and position.y into
// GTE OFX and OFY (`sll $at,$at,16; ctc2 $at, C2_OFX`), so the record's x and y ARE the projection
// centre in guest screen pixels. The pair is in the guest's 512-wide frame; the caller adds its own
// widening to x.
struct ScreenCentre {
  int32_t x = 0;
  int32_t y = 0;
};
ScreenCentre screenSpaceCentre(Core *core, uint32_t moby);

// The Moby's own scale byte. Both the shaded renderer 0x80022A2C and the regular one 0x8001F798
// apply it to the doubled view translation and nowhere else; the rotation the actor is drawn with
// is untouched by it.
constexpr uint32_t kMobyScaleByte = 0x57u;
std::array<int32_t, 3> scaledTranslation(const std::array<int32_t, 3> &doubled, uint8_t scale);

} // namespace spyro::actor_transform_math
