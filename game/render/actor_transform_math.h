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

// The Moby's own scale byte. Both the shaded renderer 0x80022A2C and the regular one 0x8001F798
// apply it to the doubled view translation and nowhere else; the rotation the actor is drawn with
// is untouched by it.
constexpr uint32_t kMobyScaleByte = 0x57u;
std::array<int32_t, 3> scaledTranslation(const std::array<int32_t, 3> &doubled, uint8_t scale);

} // namespace spyro::actor_transform_math
