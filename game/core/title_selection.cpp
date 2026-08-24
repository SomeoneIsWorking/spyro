#include "title_selection.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace spyro {
namespace {

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

std::string hexadecimal(std::uint32_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::uppercase << value;
  return stream.str();
}

SelectionResult mismatch(const ExecutableIdentity &identity, std::string detail) {
  return {SelectionStatus::IdentityMismatch,
          &identity,
          std::string(identity.serial) + " identity mismatch: " + std::move(detail)};
}

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

} // namespace

SelectionResult selectExecutable(std::string_view serial,
                                 std::span<const std::uint8_t> bytes,
                                 std::span<const ExecutableIdentity> catalog) {
  const auto found = std::ranges::find(catalog, serial, &ExecutableIdentity::serial);
  if (found == catalog.end()) {
    return {SelectionStatus::UnsupportedSerial,
            nullptr,
            "unsupported executable serial " + std::string(serial)};
  }
  const ExecutableIdentity &expected = *found;
  if (bytes.size() != expected.fileSize) {
    return mismatch(expected,
                    "expected " + std::to_string(expected.fileSize) + " bytes, got " +
                        std::to_string(bytes.size()));
  }
  constexpr std::array<std::uint8_t, 8> kPsxExeMagic{'P', 'S', '-', 'X', ' ', 'E', 'X', 'E'};
  if (bytes.size() < 0x800u || !std::ranges::equal(kPsxExeMagic, bytes.first(8))) {
    return {SelectionStatus::InvalidExecutable,
            &expected,
            std::string(expected.serial) + " is not a PS-X EXE"};
  }

  struct HeaderFact {
    std::size_t offset;
    std::uint32_t expected;
    std::string_view name;
  };
  const std::array facts{
      HeaderFact{0x10u, expected.entry, "entry"},
      HeaderFact{0x14u, expected.globalPointer, "global pointer"},
      HeaderFact{0x18u, expected.textAddress, "text address"},
      HeaderFact{0x1Cu, expected.textSize, "text size"},
      HeaderFact{0x30u, expected.stackAddress, "stack address"},
      HeaderFact{0x34u, expected.stackOffset, "stack offset"},
  };
  for (const HeaderFact &fact : facts) {
    const std::uint32_t actual = readU32(bytes, fact.offset);
    if (actual != fact.expected) {
      return mismatch(expected,
                      std::string(fact.name) + " expected " + hexadecimal(fact.expected) +
                          ", got " + hexadecimal(actual));
    }
  }

  const std::string digest = sha256(bytes);
  if (digest.empty()) {
    return {SelectionStatus::InvalidExecutable,
            &expected,
            std::string(expected.serial) + " SHA-256 calculation failed"};
  }
  if (digest != expected.sha256) {
    return mismatch(expected,
                    "SHA-256 expected " + std::string(expected.sha256) + ", got " + digest);
  }
  return {SelectionStatus::Selected,
          &expected,
          std::string(expected.serial) + " selected " + std::string(expected.displayName)};
}

SelectionResult selectExecutableFile(const std::filesystem::path &path,
                                     std::span<const ExecutableIdentity> catalog) {
  const std::string serial = path.filename().string();
  const auto found = std::ranges::find(catalog, serial, &ExecutableIdentity::serial);
  if (found == catalog.end()) {
    return {SelectionStatus::UnsupportedSerial, nullptr, "unsupported executable serial " + serial};
  }

  std::error_code sizeError;
  const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
  if (sizeError) {
    return {SelectionStatus::MissingExecutable,
            &*found,
            "cannot read " + path.string() + ": " + sizeError.message()};
  }
  if (size != found->fileSize) {
    return mismatch(*found,
                    "expected " + std::to_string(found->fileSize) + " bytes, got " +
                        std::to_string(size));
  }

  std::vector<std::uint8_t> bytes(found->fileSize);
  std::ifstream input(path, std::ios::binary);
  if (!input.read(reinterpret_cast<char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()))) {
    return {SelectionStatus::MissingExecutable, &*found, "cannot read " + path.string()};
  }
  return selectExecutable(serial, bytes, catalog);
}

} // namespace spyro
