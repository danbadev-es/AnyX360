// The launcher's pictures (box art, the player's covers, dashboard icons), decoded on a worker
// thread with WIC at the size they are drawn at (box art is up to 5000 pixels wide: never decoded
// whole), kept by path, size asked for and the file's time and size.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>

#include "wic/image.h"

namespace anyx360::launcher::ui {

namespace fs = std::filesystem;
using anyx360::ui::Image;

class ImageCache {
 public:
  // `ready` is called on the worker thread when a picture has been decoded.
  explicit ImageCache(std::function<void()> ready);
  ~ImageCache();

  // Screenshots: decode on the caller's thread right away.
  void SetSynchronous(bool synchronous) { synchronous_ = synchronous; }

  // The front cover in the picture at `path` (a wraparound's front, a front kept whole), fitted to
  // width x height pixels; null while it is being decoded, or if it can't be.
  std::shared_ptr<const Image> Cover(const fs::path& path, uint32_t width, uint32_t height);
  // The whole picture fitted to width x height pixels.
  std::shared_ptr<const Image> Picture(const fs::path& path, uint32_t width, uint32_t height);

 private:
  using Key = std::tuple<std::wstring, uint32_t, uint32_t, bool>;  // path, width, height, front
  struct Entry {
    std::shared_ptr<const Image> image;
    fs::file_time_type time;
    uintmax_t size = 0;
    bool pending = false;
    bool done = false;  // decoded, or failed: not tried again until the file changes
  };
  std::shared_ptr<const Image> Get(const fs::path& path, uint32_t width, uint32_t height, bool front);
  static std::shared_ptr<const Image> Decode(const Key& key);
  void Work();

  std::function<void()> ready_;
  bool synchronous_ = false;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::map<Key, Entry> entries_;
  std::deque<Key> queue_;
  bool stop_ = false;
  std::thread worker_;
};

}  // namespace anyx360::launcher::ui
