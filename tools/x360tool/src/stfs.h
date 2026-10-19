// STFS: the volume inside a content package's file (downloadable content, title updates, saves).
// After the header (rounded up to 0x1000) come 0x1000-byte blocks; hash tables are interleaved
// (one per 170 blocks, then per 170 tables...), and each hash entry also links a file's blocks.
// Read-only, same layout as Xenia (vfs/devices/xcontent_devices/stfs_container_device.cc).
#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "content.h"

namespace x360tool {

struct StfsEntry {
  std::string path;  // '/'-separated, relative to the root
  bool is_dir = false;
  uint32_t size = 0;
  uint32_t start_block = 0;
  uint32_t blocks = 0;
};

class StfsPackage {
 public:
  explicit StfsPackage(const fs::path& path);

  const ContentPackage& header() const { return header_; }
  // Every entry, directories before their contents.
  const std::vector<StfsEntry>& entries() const { return entries_; }
  void extract(const StfsEntry& entry, const fs::path& dest);

 private:
  uint64_t BlockOffset(uint32_t block) const;
  uint32_t HashTableBlock(uint32_t block, int level) const;
  uint32_t NextBlock(uint32_t block);
  void Read(uint64_t offset, void* out, size_t size);

  std::ifstream file_;
  uint64_t file_size_ = 0;
  ContentPackage header_;
  uint64_t data_offset_ = 0;
  uint32_t tables_per_hash_ = 1;  // 1 for read-only packages, 2 otherwise
  bool root_active_index_ = false;
  uint32_t total_blocks_ = 0;
  uint32_t step_[2] = {};
  std::vector<StfsEntry> entries_;
};

}  // namespace x360tool
