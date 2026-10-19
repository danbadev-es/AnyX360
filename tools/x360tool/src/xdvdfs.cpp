#include "xdvdfs.h"

#include <cstring>
#include <set>
#include <vector>

namespace x360tool {

namespace {

constexpr uint32_t kSectorSize = 2048;
constexpr uint32_t kVolumeDescriptorSector = 32;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicLen = sizeof(kMagic) - 1;
constexpr uint8_t kAttrDirectory = 0x10;
constexpr int kMaxDepth = 64;

struct Layout {
  uint64_t offset;
  const char* name;
};

// Known game-partition offsets (same values extract-xiso and Xenia use).
constexpr Layout kLayouts[] = {
    {0x00000000, "xiso (stripped)"},
    {0x02080000, "XGD3 (Xbox 360)"},
    {0x0FD90000, "XGD2 (Xbox 360)"},
    {0x18300000, "XGD1 (original Xbox)"},
};

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

// A disc image file; the partition starts at `base`.
class FileDevice final : public XdvdfsDevice {
 public:
  explicit FileDevice(const fs::path& path) : file_(path, std::ios::binary) {
    if (!file_) throw Error("cannot open " + path.string());
    file_.seekg(0, std::ios::end);
    size_ = static_cast<uint64_t>(file_.tellg());
  }
  uint64_t size() const { return size_; }
  uint64_t base = 0;

  void read(uint64_t offset, void* out, size_t size) override {
    offset += base;
    if (offset + size > size_) throw Error("read past end of image");
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset));
    file_.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
    if (!file_) throw Error("image read failed");
  }

 private:
  std::ifstream file_;
  uint64_t size_ = 0;
};

}  // namespace

bool is_safe_name(const std::string& name) {
  if (name.empty() || name == "." || name == "..") return false;
  for (unsigned char c : name) {
    if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
        c == '>' || c == '|')
      return false;
  }
  return true;
}

XdvdfsImage::XdvdfsImage(const fs::path& path) {
  auto file = std::make_unique<FileDevice>(path);
  for (const Layout& l : kLayouts) {
    uint64_t vd = l.offset + uint64_t(kVolumeDescriptorSector) * kSectorSize;
    if (vd + kSectorSize > file->size()) continue;
    uint8_t buf[kSectorSize];
    file->read(vd, buf, sizeof(buf));
    if (std::memcmp(buf, kMagic, kMagicLen) != 0 || std::memcmp(buf + 0x7EC, kMagic, kMagicLen) != 0) continue;
    partition_offset_ = l.offset;
    layout_ = l.name;
    root_sector_ = le32(buf + 0x14);
    root_size_ = le32(buf + 0x18);
    file->base = l.offset;
    device_ = std::move(file);
    return;
  }
  throw Error("no XDVDFS volume found (not an Xbox/Xbox 360 disc image, or an unsupported layout)");
}

XdvdfsImage::XdvdfsImage(std::unique_ptr<XdvdfsDevice> device, uint32_t root_sector, uint32_t root_size,
                         const char* layout)
    : device_(std::move(device)), layout_(layout), root_sector_(root_sector), root_size_(root_size) {}

void XdvdfsImage::walk(const std::function<void(const XdvdfsEntry&)>& visit) {
  walk_dir(root_sector_, root_size_, "", 0, visit);
}

void XdvdfsImage::walk_dir(uint32_t sector, uint32_t size, const std::string& prefix, int depth,
                           const std::function<void(const XdvdfsEntry&)>& visit) {
  if (size == 0) return;
  if (depth > kMaxDepth) throw Error("directory nesting too deep (corrupt image?)");
  if (size > 64u * 1024 * 1024) throw Error("directory table too large (corrupt image?)");

  std::vector<uint8_t> table(size);
  device_->read(uint64_t(sector) * kSectorSize, table.data(), size);

  // Entries form a binary tree; offsets are in 4-byte units from the start of the table.
  std::set<uint32_t> seen;
  std::function<void(uint32_t)> node = [&](uint32_t off4) {
    uint32_t off = off4 * 4;
    if (off + 14 > size || !seen.insert(off).second) return;
    const uint8_t* e = table.data() + off;
    uint16_t left = le16(e + 0), right = le16(e + 2);
    if (left == 0xFFFF) return;  // padding / empty directory
    uint8_t name_len = e[13];
    if (off + 14 + name_len > size) return;

    if (left) node(left);

    XdvdfsEntry entry;
    std::string name(reinterpret_cast<const char*>(e + 14), name_len);
    if (!is_safe_name(name)) throw Error("unsafe file name in image: '" + name + "'");
    entry.path = prefix.empty() ? name : prefix + "/" + name;
    entry.sector = le32(e + 4);
    entry.size = le32(e + 8);
    entry.is_dir = (e[12] & kAttrDirectory) != 0;
    visit(entry);
    if (entry.is_dir) walk_dir(entry.sector, entry.size, entry.path, depth + 1, visit);

    if (right) node(right);
  };
  node(0);
}

void XdvdfsImage::extract(const XdvdfsEntry& entry, const fs::path& dest) {
  if (dest.has_parent_path()) fs::create_directories(dest.parent_path());
  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  if (!out) throw Error("cannot write " + dest.string());

  std::vector<char> buf(4 * 1024 * 1024);
  uint64_t pos = uint64_t(entry.sector) * kSectorSize;
  uint64_t left = entry.size;
  while (left) {
    size_t n = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
    device_->read(pos, buf.data(), n);
    out.write(buf.data(), static_cast<std::streamsize>(n));
    pos += n;
    left -= n;
  }
  if (!out) throw Error("write failed: " + dest.string());
}

}  // namespace x360tool
