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
Matrix rotateForMoby(Core *core, Matrix matrix, uint32_t packedAngles);
std::array<uint32_t, 5> packMatrix(const Matrix &matrix, int16_t cr30);

psxport::native_projection::FixedAffine
worldAffine(Core *core, uint32_t moby, const Matrix &camera, std::array<int32_t, 3> &view);

} // namespace spyro::actor_transform_math
