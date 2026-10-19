#include "xex.h"

#include <cstring>

#include "x360/endian.h"

namespace x360tool {

using x360::load_be;

const char* xex_key_name(uint32_t key) {
  switch (key) {
    case xex_key::kResourceInfo: return "RESOURCE_INFO";
    case xex_key::kFileFormatInfo: return "FILE_FORMAT_INFO";
    case xex_key::kDeltaPatchDescriptor: return "DELTA_PATCH_DESCRIPTOR";
    case xex_key::kBaseReference: return "BASE_REFERENCE";
    case xex_key::kBoundingPath: return "BOUNDING_PATH";
    case xex_key::kDeviceId: return "DEVICE_ID";
    case xex_key::kOriginalBaseAddress: return "ORIGINAL_BASE_ADDRESS";
    case xex_key::kEntryPoint: return "ENTRY_POINT";
    case xex_key::kImageBaseAddress: return "IMAGE_BASE_ADDRESS";
    case xex_key::kImportLibraries: return "IMPORT_LIBRARIES";
    case xex_key::kChecksumTimestamp: return "CHECKSUM_TIMESTAMP";
    case xex_key::kEnabledForCallcap: return "ENABLED_FOR_CALLCAP";
    case xex_key::kEnabledForFastcap: return "ENABLED_FOR_FASTCAP";
    case xex_key::kOriginalPeName: return "ORIGINAL_PE_NAME";
    case xex_key::kStaticLibraries: return "STATIC_LIBRARIES";
    case xex_key::kTlsInfo: return "TLS_INFO";
    case xex_key::kDefaultStackSize: return "DEFAULT_STACK_SIZE";
    case xex_key::kDefaultFilesystemCacheSize: return "DEFAULT_FILESYSTEM_CACHE_SIZE";
    case xex_key::kDefaultHeapSize: return "DEFAULT_HEAP_SIZE";
    case xex_key::kPageHeapSizeAndFlags: return "PAGE_HEAP_SIZE_AND_FLAGS";
    case xex_key::kSystemFlags: return "SYSTEM_FLAGS";
    case xex_key::kExecutionInfo: return "EXECUTION_INFO";
    case xex_key::kServiceIdList: return "SERVICE_ID_LIST";
    case xex_key::kTitleWorkspaceSize: return "TITLE_WORKSPACE_SIZE";
    case xex_key::kGameRatings: return "GAME_RATINGS";
    case xex_key::kLanKey: return "LAN_KEY";
    case xex_key::kXbox360Logo: return "XBOX360_LOGO";
    case xex_key::kMultidiscMediaIds: return "MULTIDISC_MEDIA_IDS";
    case xex_key::kAlternateTitleIds: return "ALTERNATE_TITLE_IDS";
    case xex_key::kAdditionalTitleMemory: return "ADDITIONAL_TITLE_MEMORY";
    case xex_key::kExportsByName: return "EXPORTS_BY_NAME";
    default: return "?";
  }
}

std::string XexVersion::str() const {
  return std::to_string(major()) + "." + std::to_string(minor()) + "." + std::to_string(build()) + "." +
         std::to_string(qfe());
}

void Xex::need(uint32_t off, uint32_t size) const {
  if (uint64_t(off) + size > file_.size()) throw Error("XEX truncated or corrupt (offset out of range)");
}
uint32_t Xex::u32(uint32_t off) const {
  need(off, 4);
  return load_be<uint32_t>(file_.data() + off);
}
uint16_t Xex::u16(uint32_t off) const {
  need(off, 2);
  return load_be<uint16_t>(file_.data() + off);
}

Xex::Xex(std::vector<uint8_t> file) : file_(std::move(file)) {
  need(0, 0x18);
  if (std::memcmp(file_.data(), "XEX2", 4) != 0) {
    if (std::memcmp(file_.data(), "XEX1", 4) == 0 || std::memcmp(file_.data(), "XEX%", 4) == 0 ||
        std::memcmp(file_.data(), "XEX-", 4) == 0)
      throw Error("pre-release XEX format (not XEX2) is not supported");
    throw Error("not a XEX2 file");
  }
  module_flags_ = u32(0x04);
  pe_offset_ = u32(0x08);
  security_offset_ = u32(0x10);
  uint32_t count = u32(0x14);
  if (count > 0x1000) throw Error("implausible optional header count");
  opt_headers_.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    opt_headers_.push_back({u32(0x18 + i * 8), u32(0x18 + i * 8 + 4)});
  }

  const uint32_t s = security_offset_;
  image_size_ = u32(s + 0x004);
  image_flags_ = u32(s + 0x10C);
  load_address_ = u32(s + 0x110);
  need(s + 0x150, 16);
  std::memcpy(aes_key_.data(), file_.data() + s + 0x150, 16);
  region_ = u32(s + 0x178);
  allowed_media_ = u32(s + 0x17C);
  uint32_t page_count = u32(s + 0x180);
  if (page_count > 0x100000) throw Error("implausible page descriptor count");
  for (uint32_t i = 0; i < page_count; ++i) {
    uint32_t v = u32(s + 0x184 + i * 24);
    pages_.push_back({v >> 4, uint8_t(v & 0xF)});
  }
}

std::optional<uint32_t> Xex::opt_value(uint32_t key) const {
  for (const auto& h : opt_headers_) {
    if (h.key != key) continue;
    uint32_t kind = key & 0xFF;
    if (kind == 0x00 || kind == 0x01) return h.value;
    return u32(h.value);  // first dword of the record
  }
  return std::nullopt;
}

std::optional<uint32_t> Xex::opt_offset(uint32_t key) const {
  for (const auto& h : opt_headers_) {
    if (h.key == key && (key & 0xFF) > 0x01) {
      uint32_t size = (key & 0xFF) == 0xFF ? u32(h.value) : (key & 0xFF) * 4;
      need(h.value, size);
      return h.value;
    }
  }
  return std::nullopt;
}

uint32_t Xex::base_address() const {
  if (auto v = opt_value(xex_key::kImageBaseAddress)) return *v;
  return load_address_;
}

XexFileFormat Xex::file_format() const {
  XexFileFormat ff;
  auto off = opt_offset(xex_key::kFileFormatInfo);
  if (!off) throw Error("missing FILE_FORMAT_INFO header");
  uint32_t info_size = u32(*off);
  ff.encryption = XexEncryption(u16(*off + 4));
  ff.compression = XexCompression(u16(*off + 6));
  if (ff.compression == XexCompression::Basic) {
    uint32_t n = (info_size - 8) / 8;
    for (uint32_t i = 0; i < n; ++i) ff.basic_blocks.push_back({u32(*off + 8 + i * 8), u32(*off + 12 + i * 8)});
  } else if (ff.compression == XexCompression::Normal) {
    ff.window_size = u32(*off + 8);
    ff.first_block_size = u32(*off + 12);
    need(*off + 16, 20);
    std::memcpy(ff.first_block_hash.data(), file_.data() + *off + 16, 20);
  }
  return ff;
}

std::optional<XexExecutionInfo> Xex::execution_info() const {
  auto off = opt_offset(xex_key::kExecutionInfo);
  if (!off) return std::nullopt;
  XexExecutionInfo e;
  e.media_id = u32(*off + 0);
  e.version.raw = u32(*off + 4);
  e.base_version.raw = u32(*off + 8);
  e.title_id = u32(*off + 12);
  e.platform = file_[*off + 16];
  e.executable_type = file_[*off + 17];
  e.disc_number = file_[*off + 18];
  e.disc_count = file_[*off + 19];
  e.savegame_id = u32(*off + 20);
  return e;
}

std::vector<XexImportLibrary> Xex::import_libraries() const {
  std::vector<XexImportLibrary> libs;
  auto off = opt_offset(xex_key::kImportLibraries);
  if (!off) return libs;
  const uint32_t total = u32(*off);
  const uint32_t strings_size = u32(*off + 4);
  const uint32_t string_count = u32(*off + 8);
  const uint32_t strings = *off + 12;
  need(strings, strings_size);

  std::vector<std::string> names;
  for (uint32_t p = 0; p < strings_size && names.size() < string_count;) {
    const char* s = reinterpret_cast<const char*>(file_.data() + strings + p);
    size_t len = strnlen(s, strings_size - p);
    names.emplace_back(s, len);
    // Each name starts on a 4-byte boundary. The padding isn't always zero (Kinect Adventures has
    // "xboxkrnl.exe\0\0E\0xam.xex\0"), so align rather than skip zeros, as Xenia does.
    p = (p + uint32_t(len) + 1 + 3) & ~3u;
  }

  const uint32_t end = *off + total;
  for (uint32_t p = strings + strings_size; p + 40 <= end;) {
    uint32_t size = u32(p);
    if (size < 40) throw Error("corrupt import library record");
    XexImportLibrary lib;
    lib.id = u32(p + 24);
    lib.version.raw = u32(p + 28);
    lib.version_min.raw = u32(p + 32);
    uint16_t name_index = u16(p + 36);
    uint16_t count = u16(p + 38);
    lib.name = name_index < names.size() ? names[name_index] : "<invalid>";
    for (uint32_t i = 0; i < count; ++i) lib.record_addresses.push_back(u32(p + 40 + i * 4));
    libs.push_back(std::move(lib));
    p += size;
  }
  return libs;
}

std::vector<XexStaticLibrary> Xex::static_libraries() const {
  std::vector<XexStaticLibrary> libs;
  auto off = opt_offset(xex_key::kStaticLibraries);
  if (!off) return libs;
  uint32_t n = (u32(*off) - 4) / 16;
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t p = *off + 4 + i * 16;
    XexStaticLibrary l;
    l.name.assign(reinterpret_cast<const char*>(file_.data() + p), strnlen(reinterpret_cast<const char*>(file_.data() + p), 8));
    l.major = u16(p + 8);
    l.minor = u16(p + 10);
    l.build = u16(p + 12);
    l.tail = u16(p + 14);
    libs.push_back(std::move(l));
  }
  return libs;
}

std::optional<std::string> Xex::original_pe_name() const {
  auto off = opt_offset(xex_key::kOriginalPeName);
  if (!off) return std::nullopt;
  uint32_t size = u32(*off);
  if (size < 4) return std::nullopt;
  const char* s = reinterpret_cast<const char*>(file_.data() + *off + 4);
  return std::string(s, strnlen(s, size - 4));
}

std::vector<uint8_t> Xex::extract_with_key(const std::array<uint8_t, 16>* session_key) const {
  const XexFileFormat ff = file_format();
  if (pe_offset_ > file_.size()) throw Error("PE data offset beyond end of file");

  std::vector<uint8_t> src(file_.begin() + pe_offset_, file_.end());
  if (session_key) {
    // The whole payload is one continuous CBC stream; a trailing partial block stays as-is.
    aes128_cbc_decrypt(*session_key, std::span<uint8_t>(src.data(), src.size() & ~size_t(15)));
  }

  std::vector<uint8_t> image;
  switch (ff.compression) {
    case XexCompression::None:
      image.assign(src.begin(), src.begin() + std::min<size_t>(src.size(), image_size_));
      break;

    case XexCompression::Basic: {
      image.reserve(image_size_);
      size_t p = 0;
      for (const XexBlock& b : ff.basic_blocks) {
        if (p + b.data_size > src.size()) throw Error("basic compression block exceeds file size");
        image.insert(image.end(), src.begin() + p, src.begin() + p + b.data_size);
        image.insert(image.end(), b.zero_size, 0);
        p += b.data_size;
      }
      break;
    }

    case XexCompression::Normal: {
      // Chain of blocks: [next_block_size u32][next_block_sha1 20B] then chunks [u16 len][data]...
      // terminated by a zero-length chunk. The concatenated chunk data is one LZX stream.
      std::vector<uint8_t> lzx;
      size_t p = 0;
      uint32_t block_size = ff.first_block_size;
      while (block_size) {
        const size_t block_start = p;
        if (block_start + block_size > src.size() || block_size < 24) throw Error("LZX block exceeds file size");
        uint32_t next_size = load_be<uint32_t>(src.data() + p);
        p += 24;
        while (p + 2 <= block_start + block_size) {
          uint16_t chunk = load_be<uint16_t>(src.data() + p);
          p += 2;
          if (chunk == 0) break;
          if (p + chunk > block_start + block_size) throw Error("LZX chunk exceeds block");
          lzx.insert(lzx.end(), src.begin() + p, src.begin() + p + chunk);
          p += chunk;
        }
        p = block_start + block_size;
        block_size = next_size;
      }
      std::string error;
      image.resize(image_size_);
      if (!lzx_decompress(lzx, ff.window_size, image, error)) throw Error("LZX: " + error);
      break;
    }

    case XexCompression::Delta:
      throw Error("delta-compressed XEX (title update patch) cannot be extracted on its own");
    default:
      throw Error("unknown compression type");
  }

  image.resize(image_size_, 0);
  if (image.size() < 2 || image[0] != 'M' || image[1] != 'Z') throw Error("result is not a PE image (wrong key?)");
  return image;
}

std::vector<uint8_t> Xex::extract_image(const std::optional<std::array<uint8_t, 16>>& retail_key,
                                        std::string* used_key) const {
  if (file_format().encryption == XexEncryption::None) {
    if (used_key) *used_key = "none";
    return extract_with_key(nullptr);
  }

  std::string last_error;
  auto attempt = [&](const std::array<uint8_t, 16>& key, const char* name) -> std::optional<std::vector<uint8_t>> {
    std::array<uint8_t, 16> session = aes_key_;
    aes128_cbc_decrypt(key, session);
    try {
      auto img = extract_with_key(&session);
      if (used_key) *used_key = name;
      return img;
    } catch (const Error& e) {
      last_error = std::string(name) + " key: " + e.what();
      return std::nullopt;
    }
  };

  if (retail_key) {
    if (auto img = attempt(*retail_key, "retail")) return std::move(*img);
  }
  if (auto img = attempt(std::array<uint8_t, 16>{}, "devkit")) return std::move(*img);

  if (!retail_key) throw Error("image is encrypted and no retail key was provided (see keys/README.md)");
  throw Error("decryption failed with all keys (" + last_error + ")");
}

}  // namespace x360tool
