#include "actor_scene_builder.h"

#include "core.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <lucent/log.h>

namespace spyro::actor_scene {
namespace {

constexpr uint32_t kLevelMobys = 0x80075828u;
constexpr uint32_t kCategoryVisibility = 0x800771C8u;
constexpr uint32_t kModels = 0x80076378u;
constexpr uint32_t kCamera = 0x80076DD0u;
constexpr uint32_t kSin = 0x8006CBF8u;
constexpr uint32_t kCos = 0x8006CC78u;
constexpr uint32_t kMobySize = 0x58u;
constexpr uint32_t kMaxMobys = 4096u;

struct Matrix {
  std::array<std::array<int16_t, 3>, 3> value{};
};

Matrix unpack_matrix(Core *c) {
  const uint32_t w0 = c->mem_r32(kCamera), w1 = c->mem_r32(kCamera + 4u),
                 w2 = c->mem_r32(kCamera + 8u), w3 = c->mem_r32(kCamera + 12u),
                 w4 = c->mem_r32(kCamera + 16u);
  return {{{{(int16_t)w0, (int16_t)(w0 >> 16), (int16_t)w1},
            {(int16_t)(w1 >> 16), (int16_t)w2, (int16_t)(w2 >> 16)},
            {(int16_t)w3, (int16_t)(w3 >> 16), (int16_t)w4}}}};
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

std::array<int16_t, 3> transform_column(const Matrix &matrix, std::array<int16_t, 3> vector) {
  const auto transformed = transform(matrix, {vector[0], vector[1], vector[2]});
  return {(int16_t)std::clamp(transformed[0], -32768, 32767),
          (int16_t)std::clamp(transformed[1], -32768, 32767),
          (int16_t)std::clamp(transformed[2], -32768, 32767)};
}

void replace_columns(Matrix &matrix,
                     uint32_t first,
                     std::array<int16_t, 3> a,
                     uint32_t second,
                     std::array<int16_t, 3> b) {
  const auto transformedA = transform_column(matrix, a);
  const auto transformedB = transform_column(matrix, b);
  for (uint32_t row = 0; row < 3; ++row) {
    matrix.value[row][first] = transformedA[row];
    matrix.value[row][second] = transformedB[row];
  }
}

void rotate_y(Core *c, Matrix &matrix, uint32_t offset) {
  const int16_t sine = (int16_t)c->mem_r16(kSin + offset);
  const int16_t cosine = (int16_t)c->mem_r16(kCos + offset);
  replace_columns(matrix, 0, {cosine, 0, sine}, 2, {(int16_t)-sine, 0, cosine});
}

void rotate_x(Core *c, Matrix &matrix, uint32_t offset) {
  const int16_t sine = (int16_t)c->mem_r16(kSin + offset);
  const int16_t cosine = (int16_t)c->mem_r16(kCos + offset);
  replace_columns(matrix, 1, {0, cosine, sine}, 2, {0, (int16_t)-sine, cosine});
}

void rotate_z(Core *c, Matrix &matrix, uint32_t offset) {
  const int16_t sine = (int16_t)c->mem_r16(kSin + offset);
  const int16_t cosine = (int16_t)c->mem_r16(kCos + offset);
  replace_columns(matrix, 0, {cosine, sine, 0}, 1, {(int16_t)-sine, cosine, 0});
}

std::array<uint32_t, 5> pack_matrix(const Matrix &matrix, int16_t cr30) {
  auto pair = [](int16_t low, int16_t high) {
    return (uint16_t)low | ((uint32_t)(uint16_t)high << 16);
  };
  return {pair(matrix.value[0][0], matrix.value[0][1]),
          pair(matrix.value[0][2], matrix.value[1][0]),
          pair(matrix.value[1][1], matrix.value[1][2]),
          pair(matrix.value[2][0], matrix.value[2][1]),
          pair(matrix.value[2][2], cr30)};
}

bool regular_list_member(Core *c, uint32_t state) {
  const int8_t kind = (int8_t)(state >> 24);
  if (kind == 0 || (int32_t)state < 0) {
    return false;
  }
  const uint32_t category = (state >> 16) & 0xffu;
  return category == 0xffu || c->mem_r8(kCategoryVisibility + category) > 0;
}

bool coarse_visible(Core *c, uint32_t moby, int32_t radius) {
  const int32_t cameraX = (int32_t)c->mem_r32(kCamera + 40u);
  const int32_t cameraY = (int32_t)c->mem_r32(kCamera + 44u);
  const int32_t cameraZ = (int32_t)c->mem_r32(kCamera + 48u);
  const int32_t dx = ((int32_t)c->mem_r32(moby + 12u) - cameraX) >> 2;
  const int32_t dy = (cameraY - (int32_t)c->mem_r32(moby + 16u)) >> 2;
  const int32_t dz = (cameraZ - (int32_t)c->mem_r32(moby + 20u)) >> 2;
  return dx + radius > 0 && dx - radius < 0 && dy + radius > 0 && dy - radius < 0 &&
         dz + radius > 0 && dz - radius < 0;
}

bool view_visible(std::array<int32_t, 3> view,
                  uint32_t modelRadius,
                  int32_t radius,
                  uint32_t &flags) {
  const int32_t extent = (int32_t)modelRadius * 16;
  const int32_t horizontalNear = extent / 2 + extent / 4 + extent / 32;
  const int32_t horizontalFar = extent / 2 + (int32_t)modelRadius + extent / 32;
  const int32_t verticalNear = extent / 4 + (int32_t)modelRadius;
  const int32_t verticalFar = extent - extent / 32 - extent / 64;
  const int32_t x = view[0], y = view[1], z = view[2];
  if (z - radius >= 0 || z + extent <= 0 ||
      (std::abs(x) - horizontalNear) * 4 - (z + horizontalFar) * 3 >= 0 ||
      z + verticalNear - (std::abs(y) - verticalFar) * 3 < 1) {
    return false;
  }
  if ((std::abs(x) + horizontalNear) * 4 - (z - horizontalFar) * 3 < 0 &&
      z - verticalNear - (std::abs(y) + verticalFar) * 3 >= 1) {
    flags = 0x40000000u;
  } else {
    flags = 0x80000000u;
  }
  return true;
}

bool build_source(Core *c,
                  uint32_t moby,
                  const Matrix &cameraMatrix,
                  actor_recipe_capture::SourceRecord &source,
                  Census &census) {
  const uint16_t extentWord = c->mem_r16(moby + 80u);
  const int32_t radius = (int32_t)((extentWord & 0xffu) << 8) + (int32_t)(extentWord & 0x100u) * 2;
  if (!coarse_visible(c, moby, radius)) {
    ++census.coarseCulled;
    return false;
  }
  const uint32_t modelSet = c->mem_r32(kModels + c->mem_r16(moby + 54u) * 4u);
  const uint32_t frame = c->mem_r32(moby + 60u);
  const uint32_t descriptor = c->mem_r32(modelSet + (frame & 0xffu) * 4u + 56u);
  if (!actor_recipe_capture::physical_span(descriptor, 36u)) {
    ++census.invalidModel;
    return false;
  }
  const int32_t cameraX = (int32_t)c->mem_r32(kCamera + 40u);
  const int32_t cameraY = (int32_t)c->mem_r32(kCamera + 44u);
  const int32_t cameraZ = (int32_t)c->mem_r32(kCamera + 48u);
  const std::array<int32_t, 3> relative = {((int32_t)c->mem_r32(moby + 12u) - cameraX) >> 2,
                                           (cameraY - (int32_t)c->mem_r32(moby + 16u)) >> 2,
                                           (cameraZ - (int32_t)c->mem_r32(moby + 20u)) >> 2};
  // The handwritten MVMVA sequence loads IR1/IR2/IR3 as Y/Z/X, respectively. Preserve that
  // deliberate cyclic lane layout rather than pretending this is an ordinary XYZ matrix call.
  const std::array<int32_t, 3> view =
      transform(cameraMatrix, {relative[1], relative[2], relative[0]});
  lucent::debug("actordirect",
                "candidate moby=0x{:08X} relative=({},{},{}) view=({},{},{}) radius={} "
                "model_radius={}",
                moby,
                relative[0],
                relative[1],
                relative[2],
                view[0],
                view[1],
                view[2],
                radius,
                c->mem_r8(descriptor + 7u));
  uint32_t clipFlags = 0;
  if (!view_visible(view, c->mem_r8(descriptor + 7u), radius, clipFlags)) {
    ++census.viewCulled;
    return false;
  }
  const uint32_t animation = c->mem_r32(moby + 68u);
  const uint32_t blend = c->mem_r8(moby + 64u);
  source.header = clipFlags + blend * 0x100u + (animation >> 24) * 0x10000u +
                  (uint32_t)c->mem_r8(descriptor + 11u) * 0x1000000u + c->mem_r8(moby + 87u);
  source.descriptor = descriptor;
  source.model = descriptor + 36u + ((frame >> 16) & 0xffu) * 8u;
  if (blend != 0) {
    const uint32_t alternateDescriptor = c->mem_r32(modelSet + ((frame & 0xff00u) >> 6) + 56u);
    source.alternate = alternateDescriptor + 36u + (frame >> 24) * 8u;
  }
  source.tx = view[0];
  source.ty = view[1];
  source.tz = view[2];
  Matrix actorMatrix = cameraMatrix;
  if (const uint32_t angle = (animation >> 15) & 0x1feu) {
    rotate_y(c, actorMatrix, angle);
  }
  if (const uint32_t angle = (animation & 0xff00u) >> 7) {
    rotate_x(c, actorMatrix, angle);
  }
  if (const uint32_t angle = (animation & 0xffu) << 1) {
    rotate_z(c, actorMatrix, angle);
  }
  const int16_t cr30 = (int16_t)((int32_t)(c->mem_r8(moby + 75u) & 0x3fu) * 256 - (int16_t)view[2]);
  source.matrixWords = pack_matrix(actorMatrix, cr30);
  return true;
}

} // namespace

Status build_records(Core *c, std::vector<actor_recipe_capture::Record> &records, Census &census) {
  records.clear();
  census = {};
  const uint32_t first = c->mem_r32(kLevelMobys);
  if (!actor_recipe_capture::physical_span(first, kMobySize)) {
    return Status::InvalidMobyArray;
  }
  const Matrix cameraMatrix = unpack_matrix(c);
  for (uint32_t i = 0, moby = first; i < kMaxMobys; ++i, moby += kMobySize) {
    if (!actor_recipe_capture::physical_span(moby, kMobySize)) {
      records.clear();
      return Status::InvalidMobyArray;
    }
    const uint32_t state = c->mem_r32(moby + 72u);
    if ((int8_t)state < 0) {
      if ((state & 0xffu) == 0xffu) {
        return Status::Ready;
      }
      continue;
    }
    ++census.scanned;
    if (!regular_list_member(c, state)) {
      continue;
    }
    actor_recipe_capture::SourceRecord source{};
    if (!build_source(c, moby, cameraMatrix, source, census)) {
      ++census.culled;
      continue;
    }
    actor_recipe_capture::Record record{};
    if (records.size() == actor_recipe_capture::kDurableRecords) {
      records.clear();
      return Status::RecordCapacityExceeded;
    }
    if (!actor_recipe_capture::capture_source(c, source, record)) {
      records.clear();
      return Status::RecordCaptureRefused;
    }
    records.push_back(std::move(record));
    ++census.queued;
  }
  records.clear();
  return Status::UnterminatedMobyArray;
}

const char *status_name(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::InvalidMobyArray:
    return "invalid Moby array";
  case Status::UnterminatedMobyArray:
    return "unterminated Moby array";
  case Status::RecordCapacityExceeded:
    return "record capacity exceeded";
  case Status::RecordCaptureRefused:
    return "record capture refused";
  }
  return "unknown";
}

} // namespace spyro::actor_scene
