#include "stfs.h"

#include <algorithm>
#include <cstring>

#include "x360/endian.h"
#include "xdvdfs.h"

namespace x360tool {

namespace {

constexpr uint32_t kBlockSize = 0x1000;
constexpr uint32_t kBlocksPerHashLevel[3] = {170, 28900, 4913000};
constexpr uint32_t kEndOfChain = 0xFFFFFF;
constexpr uint32_t kEntrySize = 0x40;

uint32_t le24(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16; }

}  // namespace

StfsPackage::StfsPackage(const fs::path& path) : file_(path, std::ios::binary) {
  auto header = read_content_header(path);
  if (!header) throw Error("not a content package: " + path.string());
  if (header->volume_type != kVolumeStfs) throw Error("not an STFS package: " + path.string());
  header_ = *header;
  if (!file_) throw Error("cannot open " + path.string());
  file_.seekg(0, std::ios::end);
  file_size_ = uint64_t(file_.tellg());
  data_offset_ = (uint64_t(header_.header_size) + kBlockSize - 1) / kBlockSize * kBlockSize;

  // Volume descriptor: length 0x24, version, flags (bit 0 read-only format, bit 1 root active
  // index), file table block count (16-bit LE), first block (24-bit LE), top hash, total blocks
  // (BE), free blocks (BE).
  const uint8_t* d = header_.volume_descriptor.data();
  if (d[0] != 0x24) throw Error("unsupported STFS volume descriptor");
  tables_per_hash_ = (d[2] & 1) ? 1 : 2;
  root_active_index_ = (d[2] & 2) != 0;
  uint32_t table_blocks = uint32_t(d[3]) | uint32_t(d[4]) << 8;
  uint32_t table_block = le24(d + 5);
  total_blocks_ = x360::load_be<uint32_t>(d + 0x1C);
  step_[0] = kBlocksPerHashLevel[0] + tables_per_hash_;
  step_[1] = kBlocksPerHashLevel[1] + (kBlocksPerHashLevel[0] + 1) * tables_per_hash_;

  // The file table: 64 entries per block, chained like file data.
  std::vector<uint8_t> block(kBlockSize);
  for (uint32_t n = 0; n < table_blocks && table_block != kEndOfChain; ++n) {
    Read(BlockOffset(table_block), block.data(), kBlockSize);
    for (uint32_t i = 0; i < kBlockSize / kEntrySize; ++i) {
      const uint8_t* e = block.data() + i * kEntrySize;
      if (!e[0]) break;
      uint8_t flags = e[0x28];
      std::string name(reinterpret_cast<const char*>(e), std::min<size_t>(flags & 0x3F, 40));
      if (!is_safe_name(name)) throw Error("unsafe file name in package: '" + name + "'");
      uint16_t parent = x360::load_be<uint16_t>(e + 0x32);
      StfsEntry entry;
      if (parent == 0xFFFF) {
        entry.path = name;
      } else if (parent < entries_.size() && entries_[parent].is_dir) {
        entry.path = entries_[parent].path + "/" + name;
      } else {
        throw Error("corrupt STFS file table");
      }
      entry.is_dir = (flags & 0x80) != 0;
      entry.blocks = le24(e + 0x2C);
      entry.start_block = le24(e + 0x2F);
      entry.size = x360::load_be<uint32_t>(e + 0x34);
      entries_.push_back(entry);
    }
    table_block = NextBlock(table_block);
  }
}

void StfsPackage::Read(uint64_t offset, void* out, size_t size) {
  if (offset + size > file_size_) throw Error("read past end of package");
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(offset));
  file_.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
  if (!file_) throw Error("package read failed");
}

// Xenia's StfsContainerDevice::BlockToOffset: data block number to file offset, skipping the
// hash tables before it.
uint64_t StfsPackage::BlockOffset(uint32_t block_index) const {
  uint64_t block = block_index;
  for (uint32_t base : kBlocksPerHashLevel) {
    block += ((uint64_t(block_index) + base) / base) * tables_per_hash_;
    if (block_index < base) break;
  }
  return data_offset_ + (block << 12);
}

// Xenia's BlockToHashBlockNumber: the block holding the hash table of `level` for a data block.
uint32_t StfsPackage::HashTableBlock(uint32_t block_index, int level) const {
  if (level == 2) return step_[1];
  if (block_index < kBlocksPerHashLevel[level]) return level == 0 ? 0 : step_[level - 1];
  uint32_t block = (block_index / kBlocksPerHashLevel[level]) * step_[level];
  if (level == 0) {
    block += ((block_index / kBlocksPerHashLevel[1]) + 1) * tables_per_hash_;
    if (block_index < kBlocksPerHashLevel[1]) return block;
  }
  return block + tables_per_hash_;
}

// The next block of a chain, from the level-0 hash entry of `block_index`. Packages that keep two
// copies of each table say in the level above which one is current.
uint32_t StfsPackage::NextBlock(uint32_t block_index) {
  int top = 0;
  uint32_t secondary = root_active_index_ ? kBlockSize : 0;
  if (tables_per_hash_ == 2) {
    for (int level = 0; level < 3; ++level) {
      if (total_blocks_ < kBlocksPerHashLevel[level]) {
        top = level;
        break;
      }
    }
  } else {
    secondary = 0;
  }
  for (int level = top; level >= 1; --level) {
    uint32_t record = (block_index / kBlocksPerHashLevel[level - 1]) % kBlocksPerHashLevel[0];
    uint8_t entry[0x18];
    Read(data_offset_ + (uint64_t(HashTableBlock(block_index, level)) << 12) + secondary + record * 0x18, entry,
         sizeof(entry));
    secondary = (x360::load_be<uint32_t>(entry + 0x14) & 0x40000000) ? kBlockSize : 0;
  }
  uint32_t record = block_index % kBlocksPerHashLevel[0];
  uint8_t entry[0x18];
  Read(data_offset_ + (uint64_t(HashTableBlock(block_index, 0)) << 12) + secondary + record * 0x18, entry,
       sizeof(entry));
  return x360::load_be<uint32_t>(entry + 0x14) & 0xFFFFFF;
}

void StfsPackage::extract(const StfsEntry& entry, const fs::path& dest) {
  if (dest.has_parent_path()) fs::create_directories(dest.parent_path());
  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  if (!out) throw Error("cannot write " + dest.string());
  std::vector<char> buf(kBlockSize);
  uint32_t block = entry.start_block;
  uint32_t left = entry.size;
  while (left) {
    if (block == kEndOfChain) throw Error("STFS block chain ends early in " + entry.path);
    uint32_t n = std::min(left, kBlockSize);
    Read(BlockOffset(block), buf.data(), n);
    out.write(buf.data(), n);
    left -= n;
    if (left) block = NextBlock(block);
  }
  if (!out) throw Error("write failed: " + dest.string());
}

}  // namespace x360tool
