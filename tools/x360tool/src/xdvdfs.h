// XDVDFS (a.k.a. XISO / GDFX): the file system on Xbox and Xbox 360 game discs.
// A full Xbox 360 disc image (XGD2/XGD3) contains a video partition first; the game partition
// starts at a fixed offset that depends on the disc type. Stripped "xiso" images start at 0.
#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <string>

#include "util.h"

namespace x360tool {

struct XdvdfsEntry {
  std::string path;  // '/'-separated, relative to the root
  uint32_t sector = 0;
  uint32_t size = 0;
  bool is_dir = false;
};

class XdvdfsImage {
 public:
  explicit XdvdfsImage(const fs::path& path);

  // Offset of the game partition inside the image file and a human-readable layout name.
  uint64_t partition_offset() const { return partition_offset_; }
  const char* layout() const { return layout_; }

  // Walks every entry in directory-tree order. Directories are reported before their contents.
  void walk(const std::function<void(const XdvdfsEntry&)>& visit);

  // Copies a file entry to `dest`.
  void extract(const XdvdfsEntry& entry, const fs::path& dest);

 private:
  void read_at(uint64_t offset, void* out, size_t size);
  void walk_dir(uint32_t sector, uint32_t size, const std::string& prefix, int depth,
                const std::function<void(const XdvdfsEntry&)>& visit);

  std::ifstream file_;
  uint64_t file_size_ = 0;
  uint64_t partition_offset_ = 0;
  const char* layout_ = "unknown";
  uint32_t root_sector_ = 0;
  uint32_t root_size_ = 0;
};

// Rejects names that could escape the output directory or are invalid on Windows.
bool is_safe_name(const std::string& name);

}  // namespace x360tool
