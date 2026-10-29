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
  // Its calm idle (core/avatar_motion.h): when the picture was last drawn posed.
  avatar::Motion motion;
  double posed_at = -1;
  // The rows: the first shown (they scroll with the focus), and what left and right do on each.
  std::vector<std::function<void(int)>> steps;
  int first_row = 0;
  // Each category's choices for the gender they were listed for.
  std::map<std::pair<int, uint32_t>, std::vector<x360tool::AvatarOption>> options;
};

// Home's avatar (ui/home_page.cpp): the profile playing's, standing. Measured at rest once per size;
// while it moves, a picture per frame in that same frame (the feet stay put).
struct HomeAvatar {
  uint64_t xuid = 0;
  std::string avatar;  // what it was built from (the profile's description, base64)
  const x360tool::AvatarCatalog* pack = nullptr;
  AvatarRenderer renderer;
  bool built = false;
  std::string error;
  anyx360::ui::Image picture;
  uint32_t width = 0, height = 0;
  int attempts = 0;  // pictures drawn for one size (to fit the avatar's height to the pixel)
  float fill = 0.9f;  // the avatar's height in the picture, as a share of it
  // Where the avatar is in the picture at rest: its columns [left, right) and rows [top, bottom).
  uint32_t left = 0, right = 0, top = 0, bottom = 0;
  anyx360::ui::ComPtr<ID2D1Bitmap> bitmap;
  uint32_t bitmap_w = 0, bitmap_h = 0;
  uint64_t generation = 0;
  bool stale = true;
  // Its animation (core/avatar_motion.h): the idle; a wave when Home shows it to someone (the
  // launcher starting, back from a game, another profile signed in); a nod when it gets the focus.
  avatar::Motion motion;
  bool started = false;  // the idle's clock runs
  bool wave = true;      // waves the next time it shows
  bool focused = false;  // had the focus when last drawn
  bool shown = false;    // drawn (posed) in the last paint
  double posed_at = -1;  // when the picture was last drawn posed
};

// The avatar's animation in pictures (--anim, --anim-time, --anim-sheet, --anim-span): the idle at
// `time` seconds with `gesture` that far in; `count` frames side by side, `span` seconds apart in all
// (the gesture's length, or the idle's loop).
struct AvatarFrames {
  bool posed = false;
  avatar::Gesture gesture = avatar::Gesture::kNone;
  double time = 0, span = -1;
  int count = 1;
};

// AnyX360.exe --render-avatar: the avatar of the profile playing (or `spec`, x360tool::customize_avatar's
// syntax) alone, `width` x `height` pixels, transparent around it, into a PNG (`frames`: posed, or a
// sheet of frames). 0 on success.
int RenderAvatarFile(const std::filesystem::path& out, int width, int height, float yaw, float zoom, const std::string& spec,
                     const AvatarFrames& frames = {});

}  // namespace anyx360::launcher::ui
