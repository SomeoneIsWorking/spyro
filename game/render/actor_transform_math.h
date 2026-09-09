#pragma once

#include "native_projection.h"

#include <array>
#include <cstdint>

class Core;

namespace spyro::actor_transform_math {

struct Matrix {
  std::array<std::array<int16_t, 3>, 3> value{};
};

Matrix readCameraMatrix(Core *core);
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

} // namespace spyro::actor_transform_math
