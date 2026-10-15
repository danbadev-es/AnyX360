#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace x360tool {

namespace fs = std::filesystem;

struct Error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

std::vector<uint8_t> read_file(const fs::path& path);
void write_file(const fs::path& path, std::span<const uint8_t> data);

// Hex helpers.
std::string hex(std::span<const uint8_t> data);
std::optional<std::vector<uint8_t>> parse_hex(std::string_view text);

// AES-128-CBC decryption with a zero IV, in place. `data.size()` must be a multiple of 16.
// A single 16-byte block is equivalent to AES-128-ECB.
void aes128_cbc_decrypt(std::span<const uint8_t, 16> key, std::span<uint8_t> data);

}  // namespace x360tool
