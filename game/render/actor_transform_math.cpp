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

void rotateY(Core *core, Matrix &matrix, uint32_t offset) {
  const int16_t sine = (int16_t)core->mem_r16(kSin + offset);
  const int16_t cosine = (int16_t)core->mem_r16(kCos + offset);
  replaceColumns(matrix, 0, {cosine, 0, sine}, 2, {(int16_t)-sine, 0, cosine});
}

void rotateX(Core *core, Matrix &matrix, uint32_t offset) {
  const int16_t sine = (int16_t)core->mem_r16(kSin + offset);
  const int16_t cosine = (int16_t)core->mem_r16(kCos + offset);
  replaceColumns(matrix, 1, {0, cosine, sine}, 2, {0, (int16_t)-sine, cosine});
}

void rotateZ(Core *core, Matrix &matrix, uint32_t offset) {
  const int16_t sine = (int16_t)core->mem_r16(kSin + offset);
  const int16_t cosine = (int16_t)core->mem_r16(kCos + offset);
  replaceColumns(matrix, 0, {cosine, sine, 0}, 1, {(int16_t)-sine, cosine, 0});
}

} // namespace

Matrix readCameraMatrix(Core *core) {
  const uint32_t w0 = core->mem_r32(kCamera), w1 = core->mem_r32(kCamera + 4u),
                 w2 = core->mem_r32(kCamera + 8u), w3 = core->mem_r32(kCamera + 12u),
                 w4 = core->mem_r32(kCamera + 16u);
  return {{{{(int16_t)w0, (int16_t)(w0 >> 16), (int16_t)w1},
            {(int16_t)(w1 >> 16), (int16_t)w2, (int16_t)(w2 >> 16)},
            {(int16_t)w3, (int16_t)(w3 >> 16), (int16_t)w4}}}};
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

Matrix rotateForMoby(Core *core, Matrix matrix, uint32_t packedAngles) {
  if (const uint32_t angle = (packedAngles >> 15) & 0x1feu) {
    rotateY(core, matrix, angle);
  }
  if (const uint32_t angle = (packedAngles & 0xff00u) >> 7) {
    rotateX(core, matrix, angle);
  }
  if (const uint32_t angle = (packedAngles & 0xffu) << 1) {
    rotateZ(core, matrix, angle);
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

psxport::native_projection::FixedAffine
worldAffine(Core *core, uint32_t moby, const Matrix &camera, std::array<int32_t, 3> &view) {
  const auto relative = cameraRelativePosition(core, moby);
  // 0x80022A2C and both Moby builders load IR as Y/Z/X before MVMVA.
  view = transform(camera, {relative[1], relative[2], relative[0]});
  const Matrix rotated = rotateForMoby(core, camera, core->mem_r32(moby + 0x44u));
  psxport::native_projection::FixedAffine affine{};
  affine.m = rotated.value;
  affine.t = {view[0] * 2, view[1] * 2, view[2] * 2};
  return affine;
}

} // namespace spyro::actor_transform_math
