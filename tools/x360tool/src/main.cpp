// x360tool: offline utilities for preparing an Xbox 360 title for static recompilation.
//
//   iso-info    <image>                       Detect the disc layout
//   iso-list    <image>                       List every file in the game partition
//   iso-extract <image> <out_dir> [path...]   Extract all files (or only the given paths)
//   xex-info    <file.xex>                    Dump XEX2 headers, imports and SDK libraries
//   xex-extract <file.xex> <out.bin> [--key <file>]
//                                             Decrypt + decompress the PE image and write
//                                             <out.bin>.imports.txt and <out.bin>.sections.txt
//   xex-patch   <file.xex> <file.xexp> <out.xex>
//                                             Apply a title update's patch (default.xexp) to its
//                                             executable: the updated XEX, unencrypted and
//                                             uncompressed (what XenonRecomp reads)
//   xex-icon    <file.xex> <out.png> [--key <file>]
//                                             Write the title image (the dashboard icon)
//   content-info <package>                    A content package's header and contents
//                                             (Games on Demand, downloadable content, updates)
//   god-extract <header> <out_dir> [path...]  Extract a Games on Demand package's disc files
//   stfs-extract <package> <out_dir> [--verify]
//                                             Extract an STFS package (downloadable content, an
//                                             arcade game); --verify checks every block's SHA-1
//                                             and every hash table first
//   cover       <picture> <out.jpg> [--size WxH] [--quality Q]
//                                             A box picture's front cover, scaled to fit WxH
//                                             (default 600x900), as a JPEG (quality 0-100, 85)
//   avatar-pack <AvatarAssetPack.toc> <out_dir>
//                                             The avatar asset pack's entries (index.tsv) and
//                                             each asset's STRB blocks, unpacked
//   avatar-unpack <file> <out> [offset size]   Unpack avatar data (the container of LZX blocks the
//                                             pack's models and the avatar library's own data use)
//   avatar-model <AvatarAssetPack.toc> <entry|all> [out.obj]
//                                             Decode a part's model (or check every model in the
//                                             pack); write it as Wavefront OBJ
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>

#include "avatar.h"
#include "avatar_model.h"
#include "content.h"
#include "exports.h"
#include "wic/image.h"
#include "stfs.h"
#include "svod.h"
#include "util.h"
#include "xdbf.h"
#include "x360/endian.h"
#include "xdvdfs.h"
#include "xex.h"
#include "xex_patch.h"

using namespace x360tool;

namespace {

int usage() {
  std::fputs(
      "usage:\n"
      "  x360tool iso-info    <image.iso>\n"
      "  x360tool iso-list    <image.iso>\n"
      "  x360tool iso-extract <image.iso> <out_dir> [path ...]\n"
      "  x360tool xex-info    <default.xex>\n"
      "  x360tool xex-extract <default.xex> <out.bin> [--key <xex2_retail.key>]\n"
      "  x360tool xex-patch   <default.xex> <default.xexp> <out.xex>\n"
      "  x360tool xex-icon    <default.xex> <out.png> [--key <xex2_retail.key>]\n"
      "  x360tool content-info <package>\n"
      "  x360tool god-extract <header> <out_dir> [path ...]\n"
      "  x360tool stfs-extract <package> <out_dir> [--verify]\n"
      "  x360tool cover       <picture> <out.jpg> [--size WxH] [--quality 0-100]\n",
      stderr);
  return 2;
}

std::string fmt_size(uint64_t n) {
  char buf[32];
  if (n >= (1ull << 30)) std::snprintf(buf, sizeof(buf), "%.2f GiB", n / double(1ull << 30));
  else if (n >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.2f MiB", n / double(1ull << 20));
  else if (n >= 1024) std::snprintf(buf, sizeof(buf), "%.1f KiB", n / 1024.0);
  else std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)n);
  return buf;
}

// ---- ISO -------------------------------------------------------------------------------------

int print_volume_info(XdvdfsImage& iso) {
  uint64_t files = 0, dirs = 0, bytes = 0;
  iso.walk([&](const XdvdfsEntry& e) {
    if (e.is_dir) ++dirs;
    else { ++files; bytes += e.size; }
  });
  std::printf("layout:           %s\n", iso.layout());
  std::printf("partition offset: 0x%llX\n", (unsigned long long)iso.partition_offset());
  std::printf("contents:         %llu files, %llu directories, %s\n", (unsigned long long)files,
              (unsigned long long)dirs, fmt_size(bytes).c_str());
  return 0;
}

int cmd_iso_info(const fs::path& image) {
  XdvdfsImage iso(image);
  return print_volume_info(iso);
}

int list_volume(XdvdfsImage& iso) {
  iso.walk([&](const XdvdfsEntry& e) {
    if (e.is_dir) std::printf("%12s  %s/\n", "<dir>", e.path.c_str());
    else std::printf("%12u  %s\n", e.size, e.path.c_str());
  });
  return 0;
}

int cmd_iso_list(const fs::path& image) {
  XdvdfsImage iso(image);
  return list_volume(iso);
}

int extract_volume(XdvdfsImage& iso, const fs::path& out, const std::set<std::string>& only) {
  uint64_t count = 0, bytes = 0;
  iso.walk([&](const XdvdfsEntry& e) {
    if (e.is_dir) return;
    if (!only.empty() && !only.contains(e.path)) return;
    std::printf("  %s (%s)\n", e.path.c_str(), fmt_size(e.size).c_str());
    iso.extract(e, out / utf8_path(e.path));
    ++count;
    bytes += e.size;
  });
  std::printf("extracted %llu files, %s -> %s\n", (unsigned long long)count, fmt_size(bytes).c_str(),
              out.string().c_str());
  return count || only.empty() ? 0 : 1;
}

int cmd_iso_extract(const fs::path& image, const fs::path& out, const std::set<std::string>& only) {
  XdvdfsImage iso(image);
  return extract_volume(iso, out, only);
}

// ---- content packages (Games on Demand, downloadable content, title updates) ---------------------

int cmd_content_info(const fs::path& path) {
  auto package = read_content_header(path);
  if (!package) throw Error("not a content package: " + path.string());
  std::printf("magic:            %s\n", package->magic.c_str());
  std::printf("content type:     0x%08X (%s)\n", package->content_type, content_type_name(package->content_type));
  std::printf("title ID:         %08X\n", package->title_id);
  std::printf("name:             %s\n", package->display_name.c_str());
  std::printf("volume:           %s\n", package->volume_type == kVolumeSvod ? "SVOD" : "STFS");
  if (package->volume_type == kVolumeSvod) std::printf("data files:       %u\n", package->data_file_count);
  std::printf("thumbnail:        %s\n", package->thumbnail.empty() ? "none" : fmt_size(package->thumbnail.size()).c_str());
  if (package->volume_type == kVolumeSvod) {
    auto god = open_games_on_demand(path);
    return print_volume_info(*god);
  }
  StfsPackage stfs(path);
  uint64_t files = 0, bytes = 0;
  for (const StfsEntry& e : stfs.entries())
    if (!e.is_dir) ++files, bytes += e.size;
  std::printf("contents:         %llu files, %s\n", (unsigned long long)files, fmt_size(bytes).c_str());
  return 0;
}

int cmd_god_extract(const fs::path& header, const fs::path& out, const std::set<std::string>& only) {
  auto god = open_games_on_demand(header);
  return extract_volume(*god, out, only);
}

int cmd_stfs_extract(const fs::path& path, const fs::path& out, bool verify) {
  StfsPackage stfs(path);
  if (verify) {
    StfsVerifyResult result = stfs.verify();
    for (const std::string& error : result.errors) std::printf("  mismatch: %s\n", error.c_str());
    std::printf("verified %u data blocks and %u hash tables: %u mismatches\n", result.blocks, result.tables,
                result.mismatches);
    if (result.mismatches) throw Error("the package is damaged");
  }
  uint64_t count = 0, bytes = 0;
  for (const StfsEntry& e : stfs.entries()) {
    if (e.is_dir) continue;
    std::printf("  %s (%s)\n", e.path.c_str(), fmt_size(e.size).c_str());
    stfs.extract(e, out / utf8_path(e.path));
    ++count;
    bytes += e.size;
  }
  std::printf("extracted %llu files, %s -> %s\n", (unsigned long long)count, fmt_size(bytes).c_str(),
              out.string().c_str());
  return 0;
}

// ---- XEX -------------------------------------------------------------------------------------

const char* compression_name(XexCompression c) {
  switch (c) {
    case XexCompression::None: return "none";
    case XexCompression::Basic: return "basic (zero-fill blocks)";
    case XexCompression::Normal: return "normal (LZX)";
    case XexCompression::Delta: return "delta (patch)";
  }
  return "unknown";
}

int cmd_xex_info(const fs::path& path) {
  Xex xex(read_file(path));
  const XexFileFormat ff = xex.file_format();

  std::printf("== XEX2 header ==\n");
  std::printf("module flags:     0x%08X%s%s%s\n", xex.module_flags(), (xex.module_flags() & 1) ? " title" : "",
              (xex.module_flags() & 2) ? " exports-to-title" : "", (xex.module_flags() & 8) ? " dll" : "");
  std::printf("PE data offset:   0x%08X\n", xex.pe_offset());
  std::printf("base address:     0x%08X\n", xex.base_address());
  if (auto ep = xex.entry_point()) std::printf("entry point:      0x%08X\n", *ep);
  std::printf("image size:       0x%08X (%s)\n", xex.image_size(), fmt_size(xex.image_size()).c_str());
  std::printf("page size:        0x%X\n", xex.page_size());
  std::printf("encryption:       %s\n", ff.encryption == XexEncryption::Normal ? "AES-128 (normal)" : "none");
  std::printf("compression:      %s", compression_name(ff.compression));
  if (ff.compression == XexCompression::Normal) std::printf(", window 0x%X", ff.window_size);
  if (ff.compression == XexCompression::Basic) std::printf(", %zu blocks", ff.basic_blocks.size());
  std::printf("\nregion:           0x%08X\n", xex.region());
  std::printf("allowed media:    0x%08X\n", xex.allowed_media());
  if (auto name = xex.original_pe_name()) std::printf("original PE name: %s\n", name->c_str());

  if (auto e = xex.execution_info()) {
    std::printf("\n== Execution info ==\n");
    std::printf("title id:         %08X\n", e->title_id);
    std::printf("media id:         %08X\n", e->media_id);
    std::printf("version:          %s (base %s)\n", e->version.str().c_str(), e->base_version.str().c_str());
    std::printf("disc:             %u of %u\n", e->disc_number, e->disc_count);
    std::printf("savegame id:      %08X\n", e->savegame_id);
  }
  if (auto patch = xex.delta_patch()) {
    std::printf("\n== Delta patch (title update) ==\n");
    std::printf("updates version:  %s\n", patch->source_version.str().c_str());
    std::printf("to version:       %s\n", patch->target_version.str().c_str());
  }

  if (auto v = xex.opt_value(xex_key::kDefaultStackSize)) std::printf("default stack:    0x%X\n", *v);
  if (auto v = xex.opt_value(xex_key::kDefaultHeapSize)) std::printf("default heap:     0x%X\n", *v);
  if (auto v = xex.opt_value(xex_key::kTitleWorkspaceSize)) std::printf("title workspace:  0x%X\n", *v);
  if (auto v = xex.opt_value(xex_key::kSystemFlags)) std::printf("system flags:     0x%08X\n", *v);

  std::printf("\n== Optional headers (%zu) ==\n", xex.opt_headers().size());
  for (const auto& h : xex.opt_headers()) {
    std::printf("  %08X  %-30s value/offset 0x%08X\n", h.key, xex_key_name(h.key), h.value);
  }

  auto imports = xex.import_libraries();
  std::printf("\n== Import libraries (%zu) ==\n", imports.size());
  for (const auto& lib : imports) {
    std::printf("  %-16s v%-14s (min %s)  %zu import records\n", lib.name.c_str(), lib.version.str().c_str(),
                lib.version_min.str().c_str(), lib.record_addresses.size());
  }

  auto statics = xex.static_libraries();
  std::printf("\n== Statically linked SDK libraries (%zu) ==\n", statics.size());
  for (const auto& l : statics) {
    std::printf("  %-8s %u.%u.%u  (tail 0x%04X)\n", l.name.c_str(), l.major, l.minor, l.build, l.tail);
  }

  std::map<uint8_t, uint64_t> page_kinds;
  for (const auto& p : xex.pages()) page_kinds[p.info] += uint64_t(p.page_count) * xex.page_size();
  std::printf("\n== Page descriptors (%zu) ==\n", xex.pages().size());
  for (auto [kind, bytes] : page_kinds) {
    const char* k = kind == 1 ? "code" : kind == 2 ? "data" : kind == 3 ? "read-only" : "other";
    std::printf("  %-10s %s\n", k, fmt_size(bytes).c_str());
  }
  return 0;
}


void write_sections(const std::vector<uint8_t>& img, uint32_t base, const fs::path& out) {
  std::ostringstream s;
  if (img.size() < 0x40) return;
  uint32_t lfanew = img[0x3C] | (img[0x3D] << 8) | (img[0x3E] << 16) | (uint32_t(img[0x3F]) << 24);
  if (uint64_t(lfanew) + 24 > img.size() || std::memcmp(&img[lfanew], "PE\0\0", 4) != 0) {
    std::printf("warning: PE signature not found, skipping section list\n");
    return;
  }
  auto le16 = [&](size_t o) { return uint16_t(img[o] | (img[o + 1] << 8)); };
  auto le32 = [&](size_t o) { return uint32_t(le16(o) | (uint32_t(le16(o + 2)) << 16)); };
  const size_t fh = lfanew + 4;
  const uint16_t machine = le16(fh), nsec = le16(fh + 2), opt_size = le16(fh + 16);
  size_t sec = fh + 20 + opt_size;

  char line[160];
  std::snprintf(line, sizeof(line), "# machine 0x%04X (0x01F2 = PowerPC big-endian), %u sections\n", machine, nsec);
  s << line << "# name      guest_start  guest_end    size        flags\n";
  std::printf("\n== PE sections ==\n");
  for (uint16_t i = 0; i < nsec && sec + 40 <= img.size(); ++i, sec += 40) {
    char name[9] = {};
    std::memcpy(name, &img[sec], 8);
    uint32_t vsize = le32(sec + 8), va = le32(sec + 12), flags = le32(sec + 36);
    std::snprintf(line, sizeof(line), "%-10s  0x%08X   0x%08X   0x%08X  0x%08X%s\n", name, base + va, base + va + vsize,
                  vsize, flags, (flags & 0x20000000) ? "  [code]" : "");
    s << line;
    std::printf("  %s", line);
  }
  std::ofstream(out) << s.str();
}

// Each imported function has two records (a type-0 "variable" slot and a type-1 thunk); a true
// variable import has only the type-0 record.
void write_imports(const Xex& xex, const std::vector<uint8_t>& img, const fs::path& out) {
  std::ofstream f(out);
  f << "# library  ordinal  kind  record_address  name\n";
  const uint32_t base = xex.base_address();
  std::printf("\n== Imported ordinals ==\n");
  for (const auto& lib : xex.import_libraries()) {
    std::set<uint16_t> funcs, slots;
    size_t unknown = 0;
    for (uint32_t addr : lib.record_addresses) {
      if (addr < base || addr - base + 4 > img.size()) continue;
      uint32_t rec = x360::load_be<uint32_t>(img.data() + (addr - base));
      uint8_t type = uint8_t(rec >> 24);
      uint16_t ordinal = uint16_t(rec & 0xFFFF);
      (type == 1 ? funcs : slots).insert(ordinal);
      auto info = lookup_export(lib.name, ordinal);
      if (!info) ++unknown;
      char line[160];
      std::snprintf(line, sizeof(line), "%s  0x%03X  %s  0x%08X  %s\n", lib.name.c_str(), ordinal,
                    type == 1 ? "thunk" : type == 0 ? "var  " : "?    ", addr, info ? info->name : "<unknown>");
      f << line;
    }
    size_t vars = 0;
    for (uint16_t o : slots) vars += !funcs.contains(o);
    std::printf("  %-16s %zu functions, %zu variables", lib.name.c_str(), funcs.size(), vars);
    if (unknown) std::printf(", %zu records with unknown ordinals", unknown);
    std::printf("\n");
  }
}

int cmd_xex_extract(const fs::path& path, const fs::path& out, const std::optional<fs::path>& key_path) {
  Xex xex(read_file(path));
  auto key = load_key(key_path.value_or("keys/xex2_retail.key"), key_path.has_value());
  std::string used;
  auto img = xex.extract_image(key, &used);
  write_file(out, img);
  std::printf("wrote %s (%s, key: %s)\n", out.string().c_str(), fmt_size(img.size()).c_str(), used.c_str());

  fs::path sections = out;
  sections += ".sections.txt";
  write_sections(img, xex.base_address(), sections);
  fs::path imports = out;
  imports += ".imports.txt";
  write_imports(xex, img, imports);
  return 0;
}

// A title update applied: the base executable and its default.xexp make the updated executable
// (unencrypted, uncompressed), which the recompiler reads.
int cmd_xex_patch(const fs::path& base_path, const fs::path& patch_path, const fs::path& out) {
  const std::vector<uint8_t> base = read_file(base_path), patch = read_file(patch_path);
  const auto descriptor = Xex(patch).delta_patch();
  if (!descriptor) throw Error(patch_path.string() + " is not a title update's patch");
  const auto from = Xex(base).execution_info();
  std::printf("patch:            %s -> %s\n", descriptor->source_version.str().c_str(),
              descriptor->target_version.str().c_str());
  if (from) std::printf("executable:       %s\n", from->version.str().c_str());
  std::vector<uint8_t> patched;
  std::string error;
  if (!apply_xex_patch(base, patch, patched, error)) throw Error("cannot apply the patch: " + error);
  write_file(out, patched);
  const auto to = Xex(patched).execution_info();
  std::printf("wrote %s (%s, version %s)\n", out.string().c_str(), fmt_size(patched.size()).c_str(),
              to ? to->version.str().c_str() : "?");
  return 0;
}

int cmd_xex_icon(const fs::path& path, const fs::path& out, const std::optional<fs::path>& key_path) {
  Xex xex(read_file(path));
  auto key = load_key(key_path.value_or("keys/xex2_retail.key"), key_path.has_value());
  auto img = xex.extract_image(key);
  auto png = xex_title_image(xex, img);
  if (!png) throw Error("the XEX has no title image");
  write_file(out, *png);
  std::printf("wrote %s (%s)\n", out.string().c_str(), fmt_size(png->size()).c_str());
  return 0;
}

// The front cover of a box picture (a wraparound scan or the front alone), for the launcher.
int cmd_cover(const fs::path& in, const fs::path& out, uint32_t width, uint32_t height, int quality) {
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) throw Error("COM is not available");
  anyx360::ui::Image image;
  std::string error;
  if (!anyx360::ui::LoadFrontCover(in, width, height, image, error)) throw Error(in.string() + ": " + error);
  if (!anyx360::ui::SaveJpeg(image, out, quality / 100.0f, error)) throw Error(out.string() + ": " + error);
  std::printf("wrote %s (%ux%u, %s)\n", out.string().c_str(), image.width, image.height,
              fmt_size(fs::file_size(out)).c_str());
  CoUninitialize();
  return 0;
}

// The avatar asset pack: index.tsv (entry, the two words describing it, name, size, its blocks) and
// <entry>_<block>_<id>.bin for each block (unpacked when compressed), for studying the formats.
int cmd_avatar_pack(const fs::path& toc, const fs::path& out) {
  AvatarPack pack(read_file(toc));
  fs::create_directories(out);
  std::ofstream index(out / "index.tsv");
  index << "entry\tword0\tword1\tname\tsize\tmetadata\tblocks\n";
  size_t assets = 0, unpacked = 0;
  for (const auto& e : pack.entries()) {
    std::string name;
    for (char16_t c : e.name) name.push_back(c < 0x80 ? char(c) : '?');
    char head[64];
    std::snprintf(head, sizeof(head), "%u\t%08X\t%08X\t", e.index, e.word0, e.word1);
    index << head << name << "\t" << e.size << "\t";
    if (e.size == 0) {
      index << "\t\n";
      continue;
    }
    ++assets;
    auto data = pack.data(e);
    Strb strb;
    std::string error;
    if (!parse_strb(data, strb, error)) {
      index << "\t" << error << "\n";
      continue;
    }
    std::string blocks;
    for (size_t b = 0; b < strb.blocks.size(); ++b) {
      const StrbBlock& block = strb.blocks[b];
      auto bytes = data.subspan(block.offset, block.length);
      AvatarAssetMetadata meta;
      if (parse_avatar_metadata(bytes, block.id, meta)) {
        char text[96];
        std::snprintf(text, sizeof(text), "v%u gender %u type %u details %u category %u skeleton %u", meta.version,
                      meta.gender, meta.type, meta.details, meta.category, meta.skeleton_version);
        index << text;
      }
      std::vector<uint8_t> unpacked_data;
      uint32_t window = 0;
      std::string packed_note;
      if (is_avatar_packed(bytes)) {
        if (avatar_unpack(bytes, unpacked_data, window, error)) {
          bytes = unpacked_data;
          packed_note = "(unpacked, window " + std::to_string(window) + ")";
          ++unpacked;
        } else {
          packed_note = "(unpack failed: " + error + ")";
        }
      }
      char file[64];
      std::snprintf(file, sizeof(file), "%04u_%zu_id%u.bin", e.index, b, block.id);
      write_file(out / file, bytes);
      blocks += (blocks.empty() ? "" : " ") + std::to_string(block.id) + ":" + std::to_string(block.length) + "/" +
                std::to_string(block.field_size) + packed_note;
    }
    index << "\t" << blocks << "\n";
  }
  std::printf("%zu entries, %zu assets, %zu blocks unpacked -> %s\n", pack.entries().size(), assets, unpacked,
              out.string().c_str());
  return 0;
}

int cmd_avatar_unpack(const fs::path& in, const fs::path& out, size_t offset, size_t size) {
  auto data = read_file(in);
  if (offset > data.size()) throw Error("offset past the end of the file");
  size = std::min(size, data.size() - offset);
  std::vector<uint8_t> unpacked;
  uint32_t window = 0;
  std::string error;
  if (!avatar_unpack(std::span<const uint8_t>(data).subspan(offset, size), unpacked, window, error))
    throw Error(error);
  write_file(out, unpacked);
  std::printf("wrote %s (%s, window %u)\n", out.string().c_str(), fmt_size(unpacked.size()).c_str(), window);
  return 0;
}

// The model of a pack entry, unpacked and decoded (empty if it has none).
bool load_pack_model(const AvatarPack& pack, const AvatarPackEntry& e, std::vector<uint8_t>& file, AvatarModel& model,
                     std::string& error) {
  auto data = pack.data(e);
  Strb strb;
  if (e.size == 0 || !parse_strb(data, strb, error)) return false;
  for (const StrbBlock& block : strb.blocks) {
    if (block.id != uint32_t(StrbBlockId::kModel)) continue;
    auto bytes = data.subspan(block.offset, block.length);
    uint32_t window = 0;
    if (is_avatar_packed(bytes)) {
      if (!avatar_unpack(bytes, file, window, error)) return false;
    } else {
      file.assign(bytes.begin(), bytes.end());
    }
    return parse_avatar_model(file, model, error);
  }
  error = "no model block";
  return false;
}

int cmd_avatar_model(const fs::path& toc, const std::string& which, const std::optional<fs::path>& obj) {
  AvatarPack pack(read_file(toc));
  if (which == "all") {
    size_t models = 0, failed = 0, vertices = 0, triangles = 0;
    for (const auto& e : pack.entries()) {
      std::vector<uint8_t> file;
      AvatarModel model;
      std::string error;
      if (!load_pack_model(pack, e, file, model, error)) {
        if (error != "no model block" && e.size != 0) {
          std::printf("entry %u: %s\n", e.index, error.c_str());
          ++failed;
        }
        continue;
      }
      ++models;
      for (const auto& mesh : model.meshes) vertices += mesh.vertex_count, triangles += mesh.triangles;
    }
    std::printf("%zu models decoded, %zu failed; %zu vertices, %zu triangles\n", models, failed, vertices, triangles);
    return failed ? 1 : 0;
  }
  const uint32_t index = uint32_t(std::strtoul(which.c_str(), nullptr, 0));
  if (index >= pack.entries().size()) throw Error("no such entry");
  std::vector<uint8_t> file;
  AvatarModel model;
  std::string error;
  if (!load_pack_model(pack, pack.entries()[index], file, model, error)) throw Error(error);
  std::printf("CPU %u bytes, GPU %u bytes, %zu meshes, %zu textures, %zu welds\n", model.cpu_size(), model.gpu_size(),
              model.meshes.size(), model.textures.size(), model.welds.size());
  for (size_t m = 0; m < model.meshes.size(); ++m) {
    const auto& mesh = model.meshes[m];
    std::printf("  mesh %zu: material %u, %u vertices, %u triangles, %u UV sets, %zu parameters\n", m, mesh.material,
                mesh.vertex_count, mesh.triangles, mesh.uv_sets, mesh.params.size());
    for (const auto& p : mesh.params) {
      if (p.kind == 1) {
        std::printf("    %2u: texture %u, UV set %u, flags %08X\n", p.id, p.values[0], p.values[1], p.values[2]);
      } else {
        float f[4];
        std::memcpy(f, p.values, sizeof(f));
        std::printf("    %2u: kind %u (%g, %g, %g, %g)\n", p.id, p.kind, f[0], f[1], f[2], f[3]);
      }
    }
  }
  for (const auto& t : model.textures)
    std::printf("  texture: format %08X %ux%u, %u slices%s%s\n", t.format, t.width, t.height, t.slice_count,
                t.blank ? ", blank" : "", t.tiled ? ", tiled" : "");
  if (obj) {
    const std::string text = avatar_model_obj(model);
    write_file(*obj, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    std::printf("wrote %s\n", obj->string().c_str());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string cmd = argv[1];
  try {
    if (cmd == "iso-info") return cmd_iso_info(argv[2]);
    if (cmd == "iso-list") return cmd_iso_list(argv[2]);
    if (cmd == "iso-extract" && argc >= 4) {
      std::set<std::string> only(argv + 4, argv + argc);
      return cmd_iso_extract(argv[2], argv[3], only);
    }
    if (cmd == "xex-info") return cmd_xex_info(argv[2]);
    if (cmd == "xex-extract" && argc >= 4) {
      std::optional<fs::path> key;
      for (int i = 4; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--key") key = argv[i + 1];
      }
      return cmd_xex_extract(argv[2], argv[3], key);
    }
    if (cmd == "xex-patch" && argc >= 5) return cmd_xex_patch(argv[2], argv[3], argv[4]);
    if (cmd == "xex-icon" && argc >= 4) {
      std::optional<fs::path> key;
      for (int i = 4; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--key") key = argv[i + 1];
      }
      return cmd_xex_icon(argv[2], argv[3], key);
    }
    if (cmd == "content-info") return cmd_content_info(argv[2]);
    if (cmd == "god-extract" && argc >= 4) {
      std::set<std::string> only(argv + 4, argv + argc);
      return cmd_god_extract(argv[2], argv[3], only);
    }
    if (cmd == "stfs-extract" && argc >= 4)
      return cmd_stfs_extract(argv[2], argv[3], argc >= 5 && std::string(argv[4]) == "--verify");
    if (cmd == "avatar-pack" && argc >= 4) return cmd_avatar_pack(argv[2], argv[3]);
    if (cmd == "avatar-model" && argc >= 4)
      return cmd_avatar_model(argv[2], argv[3], argc >= 5 ? std::optional<fs::path>(argv[4]) : std::nullopt);
    if (cmd == "avatar-unpack" && argc >= 4) {
      const size_t offset = argc >= 6 ? std::strtoull(argv[4], nullptr, 0) : 0;
      const size_t size = argc >= 6 ? std::strtoull(argv[5], nullptr, 0) : SIZE_MAX;
      return cmd_avatar_unpack(argv[2], argv[3], offset, size);
    }
    if (cmd == "cover" && argc >= 4) {
      unsigned width = 600, height = 900;
      int quality = 85;
      for (int i = 4; i + 1 < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--size" && std::sscanf(argv[i + 1], "%ux%u", &width, &height) != 2) return usage();
        if (option == "--quality") quality = std::clamp(std::atoi(argv[i + 1]), 1, 100);
      }
      return cmd_cover(argv[2], argv[3], width, height, quality);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return usage();
}
