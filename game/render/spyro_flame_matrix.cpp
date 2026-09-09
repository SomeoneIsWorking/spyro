#include "spyro_flame_matrix.h"

#include "core.h"

namespace {

constexpr uint32_t kFlame = 0x800786C8u;
constexpr uint32_t kEnable = 0x9Au;
constexpr uint32_t kMatrix = 0xB8u;

} // namespace

bool spyro_flame_matrix_publish(Core *core, const std::array<uint32_t, 5> &matrix) {
  if (core == nullptr || core->mem_r8(kFlame + kEnable) == 0u) {
    return false;
  }
  for (uint32_t i = 0; i < matrix.size(); ++i) {
    core->mem_w32(kFlame + kMatrix + i * 4u, matrix[i]);
  }
  return true;
}
