// XDVDFS (a.k.a. XISO / GDFX): the file system on Xbox and Xbox 360 game discs.
// A full Xbox 360 disc image (XGD2/XGD3) contains a video partition first; the game partition
// starts at a fixed offset that depends on the disc type. Stripped "xiso" images start at 0.
// Games on Demand packages hold the same file system in their data files (svod.h): the image is
// read through an XdvdfsDevice, which maps the game partition's bytes to where they are stored.
#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

#include "util.h"

namespace x360tool {

struct XdvdfsEntry {
  std::string path;  // '/'-separated, relative to the root
  uint32_t sector = 0;
  uint32_t size = 0;
  bool is_dir = false;
};

// The game partition as a byte range: offset 0 is its first sector.
class XdvdfsDevice {
 public:
  virtual ~XdvdfsDevice() = default;
  virtual void read(uint64_t offset, void* out, size_t size) = 0;
};

class XdvdfsImage {
 public:
  // A disc image file: the layout is detected.
  explicit XdvdfsImage(const fs::path& path);
  // A partition read through `device`, whose root directory table is known.
  XdvdfsImage(std::unique_ptr<XdvdfsDevice> device, uint32_t root_sector, uint32_t root_size, const char* layout);

  // Offset of the game partition inside the image file and a human-readable layout name.
  uint64_t partition_offset() const { return partition_offset_; }
  const char* layout() const { return layout_; }

  // Walks every entry in directory-tree order. Directories are reported before their contents.
  void walk(const std::function<void(const XdvdfsEntry&)>& visit);

  // Copies a file entry to `dest`.
  void extract(const XdvdfsEntry& entry, const fs::path& dest);

 private:
  void walk_dir(uint32_t sector, uint32_t size, const std::string& prefix, int depth,
                const std::function<void(const XdvdfsEntry&)>& visit);

  std::unique_ptr<XdvdfsDevice> device_;
  uint64_t partition_offset_ = 0;
  const char* layout_ = "unknown";
  uint32_t root_sector_ = 0;
  uint32_t root_size_ = 0;
};

// Rejects names that could escape the output directory or are invalid on Windows.
bool is_safe_name(const std::string& name);

}  // namespace x360tool
