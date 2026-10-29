// Profiles' gamerpics in the system's UIs (see gamerpic.h).
#include "gamerpic.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <memory>
#include <string>

#include "stats_overlay.h"
#include "third_party/stb/stb_image.h"  // its implementation is compiled into Xenia's ImGui drawer
#include "x360/kernel.h"
#include "x360/profiles.h"
#include "xenia/ui/immediate_drawer.h"

namespace x360::gpu {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Entry {
  fs::path file;
  fs::file_time_type time{};
  std::unique_ptr<xe::ui::ImmediateTexture> texture;
  Clock::time_point checked{};
};

// Kept for the process's life (never destroyed: the drawer may be gone at exit).
std::map<uint64_t, Entry>& Cache() {
  static auto* cache = new std::map<uint64_t, Entry>();
  return *cache;
}

xe::ui::ImmediateTexture* Texture(uint64_t xuid) {
  if (!xuid) return nullptr;
  Entry& entry = Cache()[xuid];
  const Clock::time_point now = Clock::now();
  // The profile and its file looked at again every two seconds (the launcher may change them).
  if (entry.checked != Clock::time_point{} && now - entry.checked < std::chrono::seconds(2)) return entry.texture.get();
  entry.checked = now;
  fs::path file;
  if (const std::optional<profiles::Profile> profile = profiles::Load(kernel::TitlePaths().profiles_root, xuid);
      profile && !profile->gamerpic.empty())
    file = profiles::ProfileFolder(kernel::TitlePaths().profiles_root, xuid) / fs::path(reinterpret_cast<const char8_t*>(profile->gamerpic.c_str()));
  std::error_code ec;
  const fs::file_time_type time = file.empty() ? fs::file_time_type{} : fs::last_write_time(file, ec);
  if (file.empty() || ec) {
    entry = {};
    entry.checked = now;
    return nullptr;
  }
  if (entry.texture && file == entry.file && time == entry.time) return entry.texture.get();
  entry.file = file;
  entry.time = time;
  entry.texture.reset();
  // Read whole first: the path may have any characters.
  std::ifstream in(file, std::ios::binary);
  const std::string png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0, channels = 0;
  if (stbi_uc* pixels = png.empty() ? nullptr
                                    : stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(png.data()), int(png.size()),
                                                            &w, &h, &channels, 4)) {
    if (xe::ui::ImmediateDrawer* immediate = SharedImmediateDrawer())
      entry.texture = immediate->CreateTexture(uint32_t(w), uint32_t(h), xe::ui::ImmediateTextureFilter::kLinear, false, pixels);
    stbi_image_free(pixels);
  }
  return entry.texture.get();
}

}  // namespace

bool DrawGamerpic(ImDrawList* draw, ImVec2 center, float radius, uint64_t xuid) {
  xe::ui::ImmediateTexture* texture = Texture(xuid);
  if (!texture) return false;
  draw->AddImageRounded(reinterpret_cast<ImTextureID>(texture), ImVec2(center.x - radius, center.y - radius),
                        ImVec2(center.x + radius, center.y + radius), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, radius);
  return true;
}

}  // namespace x360::gpu
