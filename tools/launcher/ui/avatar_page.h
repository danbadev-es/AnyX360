// The avatar editor's state (ui/avatar_page.cpp): the profile edited, the editor's model
// (x360tool::AvatarEditor), the viewer and its picture.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "avatar_editor.h"
#include "d2d/canvas.h"
#include "ui/avatar_view.h"

namespace anyx360::launcher::ui {

struct AvatarEdit {
  uint64_t xuid = 0;
  std::shared_ptr<const x360tool::AvatarCatalog> pack;
  std::optional<x360tool::AvatarEditor> editor;
  std::vector<uint8_t> saved;  // the description when editing started or was last saved
  uint32_t revision = 0;       // bumped on every change: the viewer draws it again
  // The viewer: the avatar turned and zoomed, drawn for the revision, size and view it shows.
  AvatarRenderer renderer;
  uint32_t built_revision = ~0u;
  std::string error;
  float yaw = 0, zoom = 0;
  double turning_until = 0;  // the stick moved it lately: paint again
  anyx360::ui::Image picture;
  uint32_t picture_revision = ~0u, picture_w = 0, picture_h = 0;
  float picture_yaw = -1, picture_zoom = -1;
  anyx360::ui::ComPtr<ID2D1Bitmap> bitmap;
  uint64_t bitmap_generation = 0;
  bool bitmap_stale = true;
  anyx360::ui::RectF view{};  // where the viewer was drawn (the mouse turns it there)
  bool dragging = false;
  float drag_x = 0;
  // The rows: the first shown (they scroll with the focus), and what left and right do on each.
  std::vector<std::function<void(int)>> steps;
  int first_row = 0;
  // Each category's choices for the gender they were listed for.
  std::map<std::pair<int, uint32_t>, std::vector<x360tool::AvatarOption>> options;
};

// AnyX360.exe --render-avatar: the avatar of the profile playing (or `spec`, x360tool::customize_avatar's
// syntax) alone, `width` x `height` pixels, transparent around it, into a PNG. 0 on success.
int RenderAvatarFile(const std::filesystem::path& out, int width, int height, float yaw, float zoom, const std::string& spec);

}  // namespace anyx360::launcher::ui
