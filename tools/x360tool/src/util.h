#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
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
// The path as Windows' long form (\\?\C:\...), so files deep in a folder tree (a game's content in
// a deeply installed release) aren't limited to 260 characters.
fs::path long_path(const fs::path& path);
// Creates `path` (and its folders) for writing, long paths included; throws if it can't.
std::ofstream create_file(const fs::path& path);

// Told the bytes an extraction has written so far; returning false stops it.
using Progress = std::function<bool(uint64_t bytes)>;

// A name as the console stores it (STFS and XDVDFS file names are Windows-1252) in UTF-8.
std::string cp1252_to_utf8(std::string_view name);
// A UTF-8 path (an entry's path) as a path.
fs::path utf8_path(std::string_view utf8);

// SHA-1 of `data`.
std::array<uint8_t, 20> sha1(std::span<const uint8_t> data);

// Hex helpers.
std::string hex(std::span<const uint8_t> data);
std::optional<std::vector<uint8_t>> parse_hex(std::string_view text);

// AES-128-CBC decryption with a zero IV, in place. `data.size()` must be a multiple of 16.
// A single 16-byte block is equivalent to AES-128-ECB.
void aes128_cbc_decrypt(std::span<const uint8_t, 16> key, std::span<uint8_t> data);

// An XEX key file: 16 bytes, or 32 hex characters. Missing: nullopt, or an error if `required`.
std::optional<std::array<uint8_t, 16>> load_key(const fs::path& path, bool required);

}  // namespace x360tool
