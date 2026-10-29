// The avatar viewer: an avatar drawn with Direct3D 11 in its bind pose, as the games draw it (the
// parts x360tool::build_avatar makes: colours, face textures, hiding templates and face shapes; the
// shading recipe of docs/AVATARS.md), into a picture the launcher's canvas shows (the dashboard's
// Avatar page, the editor in desktop mode) or saves (a gamerpic). Offscreen, so the launcher's own
// overlays draw over it and screenshots have it.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "avatar_look.h"
#include "wic/image.h"

namespace anyx360::launcher::ui {

class AvatarRenderer {
 public:
  AvatarRenderer();
  ~AvatarRenderer();
  AvatarRenderer(const AvatarRenderer&) = delete;
  AvatarRenderer& operator=(const AvatarRenderer&) = delete;

  // Builds the avatar's parts and uploads them; false with `error` if it can't (no Direct3D 11, a
  // part that can't be read).
  bool SetAvatar(const x360tool::AvatarCatalog& catalog, const x360tool::AvatarDescription& description,
                 std::string& error);
  bool HasAvatar() const;

  // Draws it `width` x `height` pixels into `out` (premultiplied BGRA, transparent around it):
  // turned `yaw` radians (0: facing the camera), from the whole body (`zoom` 0) to the face (1).
  // `background` (0xAARRGGBB; 0 transparent) fills what isn't the avatar.
  bool Render(uint32_t width, uint32_t height, float yaw, float zoom, anyx360::ui::Image& out, uint32_t background = 0);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace anyx360::launcher::ui
