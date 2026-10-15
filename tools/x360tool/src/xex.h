// XEX2: the Xbox 360 executable container. It wraps a big-endian PowerPC PE image ("basefile")
// that is usually AES-encrypted and compressed. All header fields are big-endian.
// Format reference: docs/XEX2.md (cross-checked against Xenia's xex2_info.h and Free60 notes).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "util.h"

namespace x360tool {

namespace xex_key {
constexpr uint32_t kResourceInfo = 0x000002FF;
constexpr uint32_t kFileFormatInfo = 0x000003FF;
constexpr uint32_t kDeltaPatchDescriptor = 0x000005FF;
constexpr uint32_t kBaseReference = 0x00000405;
constexpr uint32_t kBoundingPath = 0x000080FF;
constexpr uint32_t kDeviceId = 0x00008105;
constexpr uint32_t kOriginalBaseAddress = 0x00010001;
constexpr uint32_t kEntryPoint = 0x00010100;
constexpr uint32_t kImageBaseAddress = 0x00010201;
constexpr uint32_t kImportLibraries = 0x000103FF;
constexpr uint32_t kChecksumTimestamp = 0x00018002;
constexpr uint32_t kEnabledForCallcap = 0x00018102;
constexpr uint32_t kEnabledForFastcap = 0x00018200;
constexpr uint32_t kOriginalPeName = 0x000183FF;
constexpr uint32_t kStaticLibraries = 0x000200FF;
constexpr uint32_t kTlsInfo = 0x00020104;
constexpr uint32_t kDefaultStackSize = 0x00020200;
constexpr uint32_t kDefaultFilesystemCacheSize = 0x00020301;
constexpr uint32_t kDefaultHeapSize = 0x00020401;
constexpr uint32_t kPageHeapSizeAndFlags = 0x00028002;
constexpr uint32_t kSystemFlags = 0x00030000;
constexpr uint32_t kExecutionInfo = 0x00040006;
constexpr uint32_t kServiceIdList = 0x000401FF;
constexpr uint32_t kTitleWorkspaceSize = 0x00040201;
constexpr uint32_t kGameRatings = 0x00040310;
constexpr uint32_t kLanKey = 0x00040404;
constexpr uint32_t kXbox360Logo = 0x000405FF;
constexpr uint32_t kMultidiscMediaIds = 0x000406FF;
constexpr uint32_t kAlternateTitleIds = 0x000407FF;
constexpr uint32_t kAdditionalTitleMemory = 0x00040801;
constexpr uint32_t kExportsByName = 0x00E10402;
}  // namespace xex_key

const char* xex_key_name(uint32_t key);

enum class XexEncryption : uint16_t { None = 0, Normal = 1 };
enum class XexCompression : uint16_t { None = 0, Basic = 1, Normal = 2, Delta = 3 };

struct XexVersion {
  uint32_t raw = 0;
  unsigned major() const { return raw >> 28; }
  unsigned minor() const { return (raw >> 24) & 0xF; }
  unsigned build() const { return (raw >> 8) & 0xFFFF; }
  unsigned qfe() const { return raw & 0xFF; }
  std::string str() const;
};

struct XexOptHeader {
  uint32_t key = 0;
  uint32_t value = 0;  // inline value, or offset into the header for larger records
};

struct XexImportLibrary {
  std::string name;
  uint32_t id = 0;
  XexVersion version, version_min;
  std::vector<uint32_t> record_addresses;  // guest addresses of import records inside the image
};

struct XexStaticLibrary {
  std::string name;
  uint16_t major = 0, minor = 0, build = 0;
  uint16_t tail = 0;  // qfe + approval type (packing not fully confirmed)
};

struct XexExecutionInfo {
  uint32_t media_id = 0;
  XexVersion version, base_version;
  uint32_t title_id = 0;
  uint8_t platform = 0, executable_type = 0, disc_number = 0, disc_count = 0;
  uint32_t savegame_id = 0;
};

struct XexBlock {
  uint32_t data_size = 0;
  uint32_t zero_size = 0;
};

struct XexFileFormat {
  XexEncryption encryption = XexEncryption::None;
  XexCompression compression = XexCompression::None;
  std::vector<XexBlock> basic_blocks;  // Basic compression
  uint32_t window_size = 0;            // Normal (LZX) compression
  uint32_t first_block_size = 0;
  std::array<uint8_t, 20> first_block_hash{};
};

struct XexPageDescriptor {
  uint32_t page_count = 0;
  uint8_t info = 0;  // 1 = code, 2 = data, 3 = read-only data
};

class Xex {
 public:
  explicit Xex(std::vector<uint8_t> file);

  const std::vector<uint8_t>& data() const { return file_; }
  uint32_t module_flags() const { return module_flags_; }
  uint32_t pe_offset() const { return pe_offset_; }
  uint32_t security_offset() const { return security_offset_; }
  const std::vector<XexOptHeader>& opt_headers() const { return opt_headers_; }

  std::optional<uint32_t> opt_value(uint32_t key) const;
  // Returns the byte offset of a non-inline optional header record, or nullopt.
  std::optional<uint32_t> opt_offset(uint32_t key) const;

  // Security info.
  uint32_t image_size() const { return image_size_; }
  uint32_t image_flags() const { return image_flags_; }
  uint32_t load_address() const { return load_address_; }
  uint32_t region() const { return region_; }
  uint32_t allowed_media() const { return allowed_media_; }
  uint32_t page_size() const { return (image_flags_ & 0x10000000) ? 0x1000 : 0x10000; }
  const std::array<uint8_t, 16>& encrypted_session_key() const { return aes_key_; }
  const std::vector<XexPageDescriptor>& pages() const { return pages_; }

  // Parsed optional headers.
  uint32_t base_address() const;
  std::optional<uint32_t> entry_point() const { return opt_value(xex_key::kEntryPoint); }
  XexFileFormat file_format() const;
  std::optional<XexExecutionInfo> execution_info() const;
  std::vector<XexImportLibrary> import_libraries() const;
  std::vector<XexStaticLibrary> static_libraries() const;
  std::optional<std::string> original_pe_name() const;

  // Decrypts (if needed) and decompresses the PE image. `retail_key` may be empty, in which case
  // only the devkit (all-zero) key is tried. Throws on failure. `used_key` receives "retail",
  // "devkit" or "none".
  std::vector<uint8_t> extract_image(const std::optional<std::array<uint8_t, 16>>& retail_key,
                                     std::string* used_key = nullptr) const;

 private:
  uint32_t u32(uint32_t off) const;
  uint16_t u16(uint32_t off) const;
  void need(uint32_t off, uint32_t size) const;
  std::vector<uint8_t> extract_with_key(const std::array<uint8_t, 16>* session_key) const;

  std::vector<uint8_t> file_;
  uint32_t module_flags_ = 0, pe_offset_ = 0, security_offset_ = 0;
  std::vector<XexOptHeader> opt_headers_;
  uint32_t image_size_ = 0, image_flags_ = 0, load_address_ = 0, region_ = 0, allowed_media_ = 0;
  std::array<uint8_t, 16> aes_key_{};
  std::vector<XexPageDescriptor> pages_;
};

// LZX decompression (lzx.cpp). Decompresses exactly `out.size()` bytes into `out`.
// Returns false if the stream is invalid; `error` explains why.
bool lzx_decompress(const std::vector<uint8_t>& in, uint32_t window_size, std::vector<uint8_t>& out,
                    std::string& error);

}  // namespace x360tool
