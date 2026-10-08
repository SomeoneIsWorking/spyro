#pragma once

#include "guest_program_image.h"
#include "image_identity.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class Core;

namespace spyro::image_publication {

// The authenticated identity of one complete byte range: the SHA-256 that names it and the 64-bit
// prefix of that digest the image catalog carries as its content identity.
struct Digest {
  std::string hex;
  std::uint64_t identity = 0;
};

// Empty when the provider could not produce a digest; the caller must refuse, because an image
// without a content identity is indistinguishable from a different load of the same address.
std::optional<Digest> digest(std::span<const std::uint8_t> bytes);

// Call AFTER the bytes are written through the canonical memory writer: that write's invalidation
// subtracts its bytes from any image already covering them. Every call mints a new generation, even
// for identical bytes, so a reload at the same address is a replacement.
psx::cpu::ImageIdentity
activate(Core &core, std::string_view kind, GuestAddressRange physical, const Digest &content);

} // namespace spyro::image_publication
