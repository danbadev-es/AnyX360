// x360tool: offline utilities for preparing an Xbox 360 title for static recompilation.
//
//   iso-info    <image>                       Detect the disc layout
//   iso-list    <image>                       List every file in the game partition
//   iso-extract <image> <out_dir> [path...]   Extract all files (or only the given paths)
//   xex-info    <file.xex>                    Dump XEX2 headers, imports and SDK libraries
//   xex-extract <file.xex> <out.bin> [--key <file>]
//                                             Decrypt + decompress the PE image and write
//                                             <out.bin>.imports.txt and <out.bin>.sections.txt
//   xex-icon    <file.xex> <out.png> [--key <file>]
//                                             Write the title image (the dashboard icon)
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "exports.h"
#include "util.h"
#include "xdbf.h"
#include "x360/endian.h"
#include "xdvdfs.h"
#include "xex.h"

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
      "  x360tool xex-icon    <default.xex> <out.png> [--key <xex2_retail.key>]\n",
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

int cmd_iso_info(const fs::path& image) {
  XdvdfsImage iso(image);
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

int cmd_iso_list(const fs::path& image) {
  XdvdfsImage iso(image);
  iso.walk([&](const XdvdfsEntry& e) {
    if (e.is_dir) std::printf("%12s  %s/\n", "<dir>", e.path.c_str());
    else std::printf("%12u  %s\n", e.size, e.path.c_str());
  });
  return 0;
}

int cmd_iso_extract(const fs::path& image, const fs::path& out, const std::set<std::string>& only) {
  XdvdfsImage iso(image);
  uint64_t count = 0, bytes = 0;
  iso.walk([&](const XdvdfsEntry& e) {
    if (e.is_dir) return;
    if (!only.empty() && !only.contains(e.path)) return;
    std::printf("  %s (%s)\n", e.path.c_str(), fmt_size(e.size).c_str());
    iso.extract(e, out / fs::path(e.path));
    ++count;
    bytes += e.size;
  });
  std::printf("extracted %llu files, %s -> %s\n", (unsigned long long)count, fmt_size(bytes).c_str(),
              out.string().c_str());
  return count || only.empty() ? 0 : 1;
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
    if (cmd == "xex-icon" && argc >= 4) {
      std::optional<fs::path> key;
      for (int i = 4; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--key") key = argv[i + 1];
      }
      return cmd_xex_icon(argv[2], argv[3], key);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return usage();
}
