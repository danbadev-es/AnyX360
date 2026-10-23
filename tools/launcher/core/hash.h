// SHA-256 through Windows' CNG (bcrypt): the .anyx manifest's file hashes.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "core/paths.h"

namespace anyx360::launcher {

// `data`'s SHA-256 in lowercase hex.
std::string Sha256(std::string_view data);
// A file's SHA-256 in lowercase hex, read in 1 MB blocks; empty if it can't be read or `progress`
// (called with the bytes read so far) returned false.
std::string Sha256File(const fs::path& file, const std::function<bool(uint64_t)>& progress = {});

}  // namespace anyx360::launcher
