#include "image_publication.h"

#include "content_identity.h"
#include "core.h"

#include <charconv>
#include <system_error>

namespace spyro::image_publication {

std::optional<Digest> digest(std::span<const std::uint8_t> bytes) {
  Digest result;
  result.hex = sha256(bytes);
  if (result.hex.size() < 16u) {
    return std::nullopt;
  }
  const auto parsed =
      std::from_chars(result.hex.data(), result.hex.data() + 16, result.identity, 16);
  if (parsed.ec != std::errc{}) {
    return std::nullopt;
  }
  return result;
}

psx::cpu::ImageIdentity
activate(Core &core, std::string_view kind, GuestAddressRange physical, const Digest &content) {
  return core.imageCatalog().activate(
      std::string(kind) + " SHA-256 " + content.hex, physical, content.identity);
}

} // namespace spyro::image_publication
