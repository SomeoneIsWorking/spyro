#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace spyro {

// Empty means the cryptographic provider could not compute the digest.
std::string sha256(std::span<const std::uint8_t> bytes);

} // namespace spyro
