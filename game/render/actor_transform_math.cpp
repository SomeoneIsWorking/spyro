#include "actor_transform_math.h"

#include "core.h"

#include <algorithm>

namespace spyro::actor_transform_math {
namespace {

constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kSin = 0x8006cbf8u;
constexpr uint32_t kCos = 0x8006cc78u;

std::array<int16_t, 3> transformColumn(const Matrix &matrix, std::array<int16_t, 3> vector) {
  const auto transformed = transform(matrix, {vector[0], vector[1], vector[2]});
  return {(int16_t)std::clamp(transformed[0], -32768, 32767),
          (int16_t)std::clamp(transformed[1], -32768, 32767),
          (int16_t)std::clamp(transformed[2], -32768, 32767)};
}

void replaceColumns(Matrix &matrix,
                    uint32_t first,
                    std::array<int16_t, 3> a,
                    uint32_t second,
                    std::array<int16_t, 3> b) {
  const auto transformedA = transformColumn(matrix, a);
  const auto transformedB = transformColumn(matrix, b);
  for (uint32_t row = 0; row < 3; ++row) {
    matrix.value[row][first] = transformedA[row];
    matrix.value[row][second] = transformedB[row];
  }
}

} // namespace

Matrix unpackMatrix(std::array<uint32_t, 5> w) {
  return {{{{(int16_t)w[0], (int16_t)(w[0] >> 16), (int16_t)w[1]},
            {(int16_t)(w[1] >> 16), (int16_t)w[2], (int16_t)(w[2] >> 16)},
            {(int16_t)w[3], (int16_t)(w[3] >> 16), (int16_t)w[4]}}}};
}

Matrix readMatrix(Core *core, uint32_t address) {
  return unpackMatrix({core->mem_r32(address),
                       core->mem_r32(address + 4u),
                       core->mem_r32(address + 8u),
                       core->mem_r32(address + 12u),
                       core->mem_r32(address + 16u)});
}

Matrix readCameraMatrix(Core *core) {
  return readMatrix(core, kCamera);
}

std::array<int32_t, 3> cameraRelativePosition(Core *core, uint32_t moby) {
  const int32_t cameraX = (int32_t)core->mem_r32(kCamera + 40u);
  const int32_t cameraY = (int32_t)core->mem_r32(kCamera + 44u);
  const int32_t cameraZ = (int32_t)core->mem_r32(kCamera + 48u);
  return {((int32_t)core->mem_r32(moby + 12u) - cameraX) >> 2,
          (cameraY - (int32_t)core->mem_r32(moby + 16u)) >> 2,
          (cameraZ - (int32_t)core->mem_r32(moby + 20u)) >> 2};
}

std::array<int32_t, 3> transform(const Matrix &matrix, std::array<int32_t, 3> vector) {
  std::array<int32_t, 3> result{};
  for (uint32_t row = 0; row < 3; ++row) {
    int64_t sum = 0;
    for (uint32_t column = 0; column < 3; ++column) {
      sum += (int64_t)matrix.value[row][column] * vector[column];
    }
    result[row] = (int32_t)(sum >> 12);
  }
  return result;
}

SineCosine sineCosine(Core *core, uint32_t tableByteOffset) {
  return {(int16_t)core->mem_r16(kSin + tableByteOffset),
          (int16_t)core->mem_r16(kCos + tableByteOffset)};
}

Matrix rotateAxis(Core *core, Matrix matrix, Axis axis, uint32_t tableByteOffset) {
  const auto [sine, cosine] = sineCosine(core, tableByteOffset);
  switch (axis) {
  case Axis::X:
    replaceColumns(matrix, 1, {0, cosine, sine}, 2, {0, (int16_t)-sine, cosine});
    break;
  case Axis::Y:
    replaceColumns(matrix, 0, {cosine, 0, sine}, 2, {(int16_t)-sine, 0, cosine});
    break;
  case Axis::Z:
    replaceColumns(matrix, 0, {cosine, sine, 0}, 1, {(int16_t)-sine, cosine, 0});
    break;
  }
  return matrix;
}

Matrix rotateForMoby(Core *core, Matrix matrix, uint32_t packedAngles) {
  if (const uint32_t angle = (packedAngles >> 15) & 0x1feu) {
    matrix = rotateAxis(core, matrix, Axis::Y, angle);
  }
  if (const uint32_t angle = (packedAngles & 0xff00u) >> 7) {
    matrix = rotateAxis(core, matrix, Axis::X, angle);
  }
  if (const uint32_t angle = (packedAngles & 0xffu) << 1) {
    matrix = rotateAxis(core, matrix, Axis::Z, angle);
  }
  return matrix;
}

std::array<uint32_t, 5> packMatrix(const Matrix &matrix, int16_t cr30) {
  const auto pair = [](int16_t low, int16_t high) {
    return (uint16_t)low | ((uint32_t)(uint16_t)high << 16);
  };
  return {pair(matrix.value[0][0], matrix.value[0][1]),
          pair(matrix.value[0][2], matrix.value[1][0]),
          pair(matrix.value[1][1], matrix.value[1][2]),
          pair(matrix.value[2][0], matrix.value[2][1]),
          pair(matrix.value[2][2], cr30)};
}

std::array<int32_t, 3> scaledTranslation(const std::array<int32_t, 3> &doubled, uint8_t scale) {
  // A zero byte means "this actor is not scaled", so retail branches around the multiply entirely
  // (0x80022CDC, 0x8001F868) rather than collapsing the actor onto the camera.
  if (scale == 0u) {
    return doubled;
  }
  // GPF with sf=0 multiplies each IR by IR0 with no pre-shift and both renderers read MAC back with
  // `sra 5`. mtc2 into IR1..IR3 keeps only the low 16 bits, so a far enough actor wraps here
  // exactly as it does on hardware; widening it would silently disagree with the guest on the same
  // input.
  const auto component = [scale](int32_t value) {
    return (int32_t)((int32_t)(int16_t)value * (int32_t)scale) >> 5;
  };
  return {component(doubled[0]), component(doubled[1]), component(doubled[2])};
}

psxport::native_projection::FixedAffine
worldAffine(Core *core, uint32_t moby, const Matrix &camera, std::array<int32_t, 3> &view) {
  const auto relative = cameraRelativePosition(core, moby);
  // 0x80022A2C and both Moby builders load IR as Y/Z/X before MVMVA.
  view = transform(camera, {relative[1], relative[2], relative[0]});
  const Matrix rotated = rotateForMoby(core, camera, core->mem_r32(moby + 0x44u));
  psxport::native_projection::FixedAffine affine{};
  affine.m = rotated.value;
  affine.t = scaledTranslation({view[0] * 2, view[1] * 2, view[2] * 2},
                               (uint8_t)core->mem_r8(moby + kMobyScaleByte));
  return affine;
}

} // namespace spyro::actor_transform_math
