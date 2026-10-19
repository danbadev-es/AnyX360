#include "svod.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace x360tool {

namespace {

constexpr uint64_t kBlockSize = 0x800;
constexpr uint64_t kHashBlockSize = 0x1000;
constexpr uint64_t kBlocksPerL0Hash = 0x198;
constexpr uint64_t kHashesPerL1Hash = 0xA1C4;
constexpr uint64_t kBlocksPerFile = 0x14388;
constexpr uint64_t kMaxFileSize = 0xA290000;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicLen = sizeof(kMagic) - 1;

enum class Layout { kEnhancedGdf, kXsf, kSingleFile, kMultipleFiles };

uint32_t le24(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16; }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

class SvodDevice final : public XdvdfsDevice {
 public:
  SvodDevice(const fs::path& header, const ContentPackage& package) {
    // Descriptor: ..., +0x18 features (bit 6: enhanced GDF layout), +0x1C start data block (24-bit LE).
    enhanced_gdf_ = (package.volume_descriptor[0x18] & 0x40) != 0;
    start_block_ = le24(package.volume_descriptor.data() + 0x1C);
    fs::path data = header;
    data += ".data";
    for (uint32_t i = 0; i < package.data_file_count; ++i) {
      char name[16];
      std::snprintf(name, sizeof(name), "Data%04u", i);
      fs::path path = data / name;
      auto file = std::make_unique<std::ifstream>(path, std::ios::binary);
      if (!*file) throw Error("missing data file: " + path.string());
      files_.push_back(std::move(file));
    }
    if (files_.empty()) throw Error("the package has no data files");
  }

  // Finds the magic block like Xenia does and returns where it is in Data0000.
  uint64_t DetectLayout() {
    if (enhanced_gdf_) {
      if (!MagicAt(0x2000)) throw Error("Games on Demand package with an enhanced GDF layout but no volume found");
      layout_ = Layout::kEnhancedGdf;
      return 0x2000;
    }
    if (MagicAt(0x12000)) {
      layout_ = Layout::kXsf;
      return 0x12000;
    }
    uint64_t magic = files_.size() == 1 ? 0xD000 : 0x2000;
    if (!MagicAt(magic)) throw Error("no volume found in the Games on Demand package");
    layout_ = files_.size() == 1 ? Layout::kSingleFile : Layout::kMultipleFiles;
    return magic;
  }

  const char* layout_name() const {
    switch (layout_) {
      case Layout::kEnhancedGdf: return "Games on Demand (enhanced GDF)";
      case Layout::kXsf: return "Games on Demand (XSF)";
      case Layout::kSingleFile: return "Games on Demand (single file)";
      default: return "Games on Demand";
    }
  }

  void ReadFile(size_t index, uint64_t offset, void* out, size_t size) {
    if (index >= files_.size()) throw Error("read past the last data file");
    std::ifstream& file = *files_[index];
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
    if (!file) throw Error("data file read failed");
  }

  void read(uint64_t offset, void* out, size_t size) override {
    auto* dest = static_cast<uint8_t*>(out);
    while (size) {
      // A run of blocks stored one after another in the same data file.
      uint64_t block = offset / kBlockSize;
      size_t file = 0;
      uint64_t address = 0;
      Map(block, file, address);
      uint64_t start = address + offset % kBlockSize;
      size_t run = size_t(std::min<uint64_t>(size, kBlockSize - offset % kBlockSize));
      while (run < size) {
        size_t next_file = 0;
        uint64_t next_address = 0;
        Map(block + 1, next_file, next_address);
        if (next_file != file || next_address != address + kBlockSize) break;
        ++block;
        address = next_address;
        run = size_t(std::min<uint64_t>(size, run + kBlockSize));
      }
      ReadFile(file, start, dest, run);
      dest += run;
      offset += run;
      size -= run;
    }
  }

 private:
  bool MagicAt(uint64_t offset) {
    char buf[kMagicLen];
    std::ifstream& file = *files_[0];
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(buf, kMagicLen);
    return file && std::memcmp(buf, kMagic, kMagicLen) == 0;
  }

  // Xenia's SvodContainerDevice::BlockToOffset.
  void Map(uint64_t block, size_t& file_index, uint64_t& address) const {
    uint64_t true_block = block - uint64_t(start_block_) * 2;
    if (layout_ == Layout::kEnhancedGdf) true_block += 2;  // the volume starts 0x1000 bytes in
    uint64_t file_block = true_block % kBlocksPerFile;
    file_index = size_t(true_block / kBlocksPerFile);
    uint64_t level0_tables = file_block / kBlocksPerL0Hash + 1;
    uint64_t level1_tables = level0_tables / kHashesPerL1Hash + 1;
    uint64_t offset = (level0_tables + level1_tables) * kHashBlockSize;
    if (layout_ == Layout::kSingleFile) offset += 0xB000;
    address = file_block * kBlockSize + offset;
    if (address >= kMaxFileSize) {
      file_index += 1;
      address = address % kMaxFileSize + 0x2000;
    }
  }

  std::vector<std::unique_ptr<std::ifstream>> files_;
  bool enhanced_gdf_ = false;
  uint32_t start_block_ = 0;
  Layout layout_ = Layout::kMultipleFiles;
};

}  // namespace

std::unique_ptr<XdvdfsImage> open_games_on_demand(const fs::path& header, ContentPackage* package_out) {
  auto package = read_content_header(header);
  if (!package) throw Error("not a content package: " + header.string());
  if (package->volume_type != kVolumeSvod)
    throw Error("not a Games on Demand package (its volume is STFS): " + header.string());
  auto device = std::make_unique<SvodDevice>(header, *package);
  uint64_t magic = device->DetectLayout();
  // The volume descriptor: magic, then the root directory's sector and size (little-endian).
  uint8_t root[8];
  device->ReadFile(0, magic + 0x14, root, sizeof(root));
  const char* layout = device->layout_name();
  if (package_out) *package_out = *package;
  return std::make_unique<XdvdfsImage>(std::move(device), le32(root), le32(root + 4), layout);
}

}  // namespace x360tool
