#include "ui/images.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>

namespace anyx360::launcher::ui {

ImageCache::ImageCache(std::function<void()> ready) : ready_(std::move(ready)), worker_([this] { Work(); }) {}

ImageCache::~ImageCache() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  worker_.join();
}

std::shared_ptr<const Image> ImageCache::Cover(const fs::path& path, uint32_t width, uint32_t height) {
  return Get(path, width, height, true);
}

std::shared_ptr<const Image> ImageCache::Picture(const fs::path& path, uint32_t width, uint32_t height) {
  return Get(path, width, height, false);
}

std::shared_ptr<const Image> ImageCache::Get(const fs::path& path, uint32_t width, uint32_t height, bool front) {
  if (path.empty() || !width || !height) return nullptr;
  std::error_code ec;
  const fs::file_time_type time = fs::last_write_time(path, ec);
  if (ec) return nullptr;
  const uintmax_t size = fs::file_size(path, ec);
  // Sizes in steps of 16 pixels: a window being resized doesn't decode at every size.
  Key key{path.wstring(), (width + 15) / 16 * 16, (height + 15) / 16 * 16, front};
  std::unique_lock lock(mutex_);
  Entry& entry = entries_[key];
  const bool current = entry.time == time && entry.size == size && (entry.done || entry.pending);
  if (current) return entry.image;
  entry.time = time;
  entry.size = size;
  if (synchronous_) {
    lock.unlock();
    std::shared_ptr<const Image> image = Decode(key);
    lock.lock();
    entries_[key].image = image;
    entries_[key].done = true;
    return image;
  }
  entry.pending = true;
  entry.done = false;
  queue_.push_back(key);
  wake_.notify_one();
  return entry.image;  // the previous version, if the file changed
}

std::shared_ptr<const Image> ImageCache::Decode(const Key& key) {
  const auto& [path, width, height, front] = key;
  auto image = std::make_shared<Image>();
  std::string error;
  const bool decoded = front ? anyx360::ui::LoadFrontCover(path, width, height, *image, error)
                             : anyx360::ui::DecodePicture(path, *image, error, width, height);
  return decoded ? image : nullptr;
}

void ImageCache::Work() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // WIC
  for (;;) {
    Key key;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
      if (stop_) break;
      key = queue_.front();
      queue_.pop_front();
    }
    std::shared_ptr<const Image> image = Decode(key);
    {
      std::lock_guard lock(mutex_);
      Entry& entry = entries_[key];
      entry.image = image;
      entry.pending = false;
      entry.done = true;
    }
    if (ready_) ready_();
  }
  CoUninitialize();
}

}  // namespace anyx360::launcher::ui
