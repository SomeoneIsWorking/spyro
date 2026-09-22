#include "gte_color_ops.h"

namespace spyro::gte_color {
namespace {

// i32_to_i16_saturate with lm=0, and the i44 truncation applied to the intermediate.
int64_t truncate44(int64_t value) {
  return (int64_t)((uint64_t)value << (64 - 44)) >> (64 - 44);
}

int32_t saturate16(int32_t value) {
  if (value < -32768) {
    return -32768;
  }
  if (value > 32767) {
    return 32767;
  }
  return value;
}

uint32_t clampByte(int32_t value) {
  if (value < 0) {
    return 0u;
  }
  if (value > 255) {
    return 255u;
  }
  return (uint32_t)value;
}

// The shared body of both operations: three channels, each pulled toward its far colour by IR0.
void interpolate(const int32_t source[3], Vector3 farColor, int32_t ir0, int32_t mac[3]) {
  const int32_t fc[3] = {farColor.x, farColor.y, farColor.z};
  for (int i = 0; i < 3; ++i) {
    mac[i] = (int32_t)(truncate44(((int64_t)fc[i] << 12) - ((int64_t)source[i] << 12)) >> 12);
    mac[i] =
        (int32_t)(truncate44(((int64_t)source[i] << 12) + (int64_t)ir0 * saturate16(mac[i])) >> 12);
  }
}

} // namespace

Vector3 intpl(Vector3 ir, Vector3 farColor, int32_t ir0) {
  const int32_t source[3] = {ir.x, ir.y, ir.z};
  int32_t mac[3] = {0, 0, 0};
  interpolate(source, farColor, ir0, mac);
  return {mac[0], mac[1], mac[2]};
}

uint32_t dpcs(uint32_t rgb, Vector3 farColor, int32_t ir0) {
  const int32_t source[3] = {(int32_t)((rgb >> 0) & 0xffu) << 4,
                             (int32_t)((rgb >> 8) & 0xffu) << 4,
                             (int32_t)((rgb >> 16) & 0xffu) << 4};
  int32_t mac[3] = {0, 0, 0};
  interpolate(source, farColor, ir0, mac);
  return clampByte(mac[0] >> 4) | (clampByte(mac[1] >> 4) << 8) | (clampByte(mac[2] >> 4) << 16) |
         (rgb & 0xff000000u);
}

} // namespace spyro::gte_color
