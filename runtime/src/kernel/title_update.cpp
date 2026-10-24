// Title updates at start: the base executable patched with the update's .xexp (x360tool's
// xex_patch.h, XenonRecomp's patcher), cached so it's done once per base and update.
#include <system_error>

#include "x360/kernel.h"
#include "xex.h"
#include "xex_patch.h"

namespace x360::kernel {

namespace fs = std::filesystem;

namespace {

std::string Hash(const std::vector<uint8_t>& data) {
  const auto digest = x360tool::sha1(data);
  return x360tool::hex(std::span<const uint8_t>(digest.data(), 8));
}

std::string VersionText(uint32_t version) { return x360tool::XexVersion{version}.str(); }

}  // namespace

PreparedUpdate PrepareTitleUpdate(const fs::path& base, const fs::path& patch, uint32_t version,
                                  const fs::path& cache_dir) {
  PreparedUpdate result;
  std::vector<uint8_t> base_bytes, patch_bytes;
  uint32_t base_version = 0;
  x360tool::XexDeltaPatch descriptor;
  try {
    base_bytes = x360tool::read_file(base);
    patch_bytes = x360tool::read_file(patch);
    const auto info = x360tool::Xex(base_bytes).execution_info();
    if (!info) throw x360tool::Error(base.filename().string() + " has no execution info");
    base_version = info->version.raw;
    const auto delta = x360tool::Xex(patch_bytes).delta_patch();
    if (!delta) throw x360tool::Error(patch.filename().string() + " is not a title update's patch");
    descriptor = *delta;
  } catch (const std::exception& e) {
    result.error = e.what();
    return result;
  }
  if (descriptor.source_version.raw != base_version) {
    result.error = "the update is for version " + descriptor.source_version.str() + ", and " +
                   base.filename().string() + " is version " + VersionText(base_version);
    return result;
  }
  if (version && descriptor.target_version.raw != version) {
    result.error = "the update makes version " + descriptor.target_version.str() + ", and this executable is built for " +
                   VersionText(version);
    return result;
  }

  // <name>-<base>-<patch>.xex: a new copy of the game or of the update gets its own file.
  const std::string stem = base.stem().string();
  const fs::path cached = cache_dir / (stem + "-" + Hash(base_bytes) + "-" + Hash(patch_bytes) + ".xex");
  std::error_code ec;
  if (fs::is_regular_file(cached, ec) && fs::file_size(cached, ec) > 0x18) {
    result.xex = cached;
    result.cached = true;
    return result;
  }
  std::vector<uint8_t> patched;
  std::string error;
  if (!x360tool::apply_xex_patch(base_bytes, patch_bytes, patched, error)) {
    result.error = error;
    return result;
  }
  try {
    const auto info = x360tool::Xex(patched).execution_info();
    if (!info || info->version.raw != descriptor.target_version.raw)
      throw x360tool::Error("the patched executable is not version " + descriptor.target_version.str());
    // Written beside it and renamed, so a cut-short write never looks like a cached file.
    fs::create_directories(cache_dir, ec);
    fs::path partial = cached;
    partial += ".partial";
    x360tool::write_file(partial, patched);
    fs::rename(partial, cached, ec);
    if (ec) {
      fs::remove(partial, ec);
      throw x360tool::Error("cannot write " + cached.string());
    }
  } catch (const std::exception& e) {
    result.error = e.what();
    return result;
  }
  // Earlier copies of this executable (another base or update) aren't used again.
  for (auto it = fs::directory_iterator(cache_dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (it->path() != cached && name.size() > stem.size() + 1 && name.compare(0, stem.size() + 1, stem + "-") == 0 &&
        it->path().extension() == ".xex") {
      std::error_code ignored;
      fs::remove(it->path(), ignored);
    }
  }
  result.xex = cached;
  return result;
}

}  // namespace x360::kernel
