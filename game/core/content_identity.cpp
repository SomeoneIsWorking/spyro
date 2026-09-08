#include "content_identity.h"

#include <openssl/evp.h>

#include <array>
#include <iomanip>
#include <sstream>

namespace spyro {

std::string sha256(std::span<const std::uint8_t> bytes) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digestSize = 0;
  if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &digestSize, EVP_sha256(), nullptr) !=
          1 ||
      digestSize != 32u) {
    return {};
  }
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (unsigned int index = 0; index < digestSize; ++index) {
    stream << std::setw(2) << static_cast<unsigned int>(digest[index]);
  }
  return stream.str();
}

} // namespace spyro
