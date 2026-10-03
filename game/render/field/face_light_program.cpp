#include "face_light_program.h"

#include "guest_magnitude.h"

#include <algorithm>

namespace spyro::face_light {
namespace {

// The four GTE operations the arm issues, with the shift and saturation flags taken from their
// encoded words rather than from the mnemonic: OP 0x4B70000C (sf=0, lm=0), SQR 0x4AA00428
// (sf=0, lm=1), GPF 0x4B90003D (sf=0, lm=0), CC 0x4B38041C (sf=1, lm=1).

using Vector = std::array<std::int32_t, 3>;

// MAC1..3 are 32 bit. A wider intermediate is truncated into them, and the overflow lands in FLAG,
// which this program never reads.
std::int32_t mac(std::int64_t value) {
  return (std::int32_t)(std::uint32_t)value;
}

// `mtc2` into IR1..3 or IR0 keeps the low 16 bits, so every value entering the GTE here is
// truncated first. This is not saturation: the register is simply 16 bits wide.
std::int16_t ir(std::int32_t value) {
  return (std::int16_t)(std::uint16_t)(std::uint32_t)value;
}

// The three view-space edge components, each truncated on its way into the GTE.
Vector edge(const ViewVertex &from, const ViewVertex &to) {
  return {ir(to.x - from.x), ir(to.y - from.y), ir(to.z - from.z)};
}

// OP: the cross product of IR1..3 with the rotation matrix diagonal RT11/RT22/RT33, which the arm
// loads with the second edge. sf=0, so nothing is shifted out.
Vector cross(const Vector &ir123, const Vector &diagonal) {
  return {mac((std::int64_t)diagonal[1] * ir123[2] - (std::int64_t)diagonal[2] * ir123[1]),
          mac((std::int64_t)diagonal[2] * ir123[0] - (std::int64_t)diagonal[0] * ir123[2]),
          mac((std::int64_t)diagonal[0] * ir123[1] - (std::int64_t)diagonal[1] * ir123[0])};
}

// R3000A `div` by zero is defined: HI keeps the dividend and LO becomes -1 for a non-negative one.
// The arm divides without checking, so reproducing the hardware answer is what keeps a degenerate
// face looking the way it does on console rather than inventing a value for it.
std::int32_t divide(std::int32_t dividend, std::int32_t divisor) {
  if (divisor == 0) {
    return dividend >= 0 ? -1 : 1;
  }
  return dividend / divisor;
}

// CC's second half: the RGBC channel, fixed at 0xFF here, times the saturated IR, shifted left 4,
// right 12 by sf, then right 4 again into the byte RGB2 keeps.
std::uint32_t color_channel(std::int32_t background, const LightColor &light, const Vector &term) {
  const std::int64_t lit = (std::int64_t)background * 0x1000 + (std::int64_t)light.first * term[0] +
                           (std::int64_t)light.second * term[1] +
                           (std::int64_t)light.third * term[2];
  const std::int32_t saturated = std::clamp(mac(lit >> 12), 0, 0x7fff);
  return (std::uint32_t)std::clamp((0xff * saturated) >> 12, 0, 0xff);
}

// `.L80021FE0`: one constant out of the control word's bits 16..23 is added to every vertex's red
// with saturation at 0xFF and subtracted from its green and blue with a floor of zero. A zero
// constant is the identity, which is exactly what the arm's own zero-constant branch writes, so the
// two branches are one expression here. Each channel is stored with `sb`, so each stays a byte.
Result tint(const std::array<std::uint32_t, 3> &material, std::uint32_t control) {
  const std::uint32_t step = (control >> 16) & 0xffu;
  Result out{Status::Ready, {}, true};
  for (std::size_t i = 0; i < out.color.size(); ++i) {
    const std::uint32_t red = material[i] & 0xffu;
    const std::uint32_t green = (material[i] >> 8) & 0xffu;
    const std::uint32_t blue = (material[i] >> 16) & 0xffu;
    out.color[i] = std::min(red + step, 0xffu) | ((green > step ? green - step : 0u) << 8) |
                   ((blue > step ? blue - step : 0u) << 16);
  }
  return out;
}

Result uniform(Status status, std::uint32_t color) {
  return {status, {color, color, color}, false};
}

} // namespace

const char *status_name(Status status) {
  switch (status) {
  case Status::Ready:
    return "Ready";
  case Status::NoEnvironment:
    return "NoEnvironment";
  case Status::Degenerate:
    return "Degenerate";
  }
  return "unknown";
}

Result face_color(const std::array<ViewVertex, 3> &view,
                  const std::array<std::uint32_t, 3> &material,
                  std::uint32_t control,
                  const Environment &environment) {
  if (((std::int32_t)control >> 24) != 0) {
    return tint(material, control);
  }
  if (environment.magnitude.size() != kMagnitudeEntries) {
    return uniform(Status::NoEnvironment, 0);
  }

  const Vector normal = cross(edge(view[0], view[1]), edge(view[0], view[2]));

  // SQR over the normal's components, with 2 and 3 deliberately swapped on their way in. The swap
  // does not change the sum of squares, but it does decide which component each later light-matrix
  // column multiplies, so it is preserved rather than tidied away.
  const Vector term = {ir(normal[0] >> 4), ir(normal[2] >> 4), ir(normal[1] >> 4)};
  const std::uint32_t sum = (std::uint32_t)mac((std::int64_t)term[0] * term[0]) +
                            (std::uint32_t)mac((std::int64_t)term[1] * term[1]) +
                            (std::uint32_t)mac((std::int64_t)term[2] * term[2]);
  if (sum == 0u) {
    return uniform(Status::Degenerate, 0);
  }

  const auto step = guest_magnitude::normalize(sum, guest_magnitude::lzcr(sum));
  if (step.tableByteOffset >= kMagnitudeEntries * sizeof(std::int16_t) ||
      (step.tableByteOffset & 1u) != 0u) {
    return uniform(Status::Degenerate, 0);
  }
  const std::int32_t length =
      (std::int32_t)(guest_magnitude::scaled(environment.magnitude[step.tableByteOffset / 2u],
                                             step.exponent) >>
                     12);

  // GPF: the intensity packed into the control word's bits 18..23, scaled up and divided by the
  // normal's own length, then multiplied through each component.
  const std::int16_t intensity = ir(divide((std::int32_t)((control >> 18) << 14), length));
  const Vector weighted = {ir(mac((std::int64_t)intensity * term[0]) >> 8),
                           ir(mac((std::int64_t)intensity * term[1]) >> 8),
                           ir(mac((std::int64_t)intensity * term[2]) >> 8)};

  // CC, over the material colour the same control word carries as the GTE background.
  const std::int32_t red = (std::int32_t)((control >> 6) & 0xff0u);
  const std::int32_t green = (std::int32_t)(control & 0xff0u);
  const std::int32_t blue = (std::int32_t)((control << 6) & 0xff0u);
  const std::uint32_t color = color_channel(red, environment.light, weighted) |
                              (color_channel(green, environment.light, weighted) << 8) |
                              (color_channel(blue, environment.light, weighted) << 16);
  return uniform(Status::Ready, color);
}

} // namespace spyro::face_light
