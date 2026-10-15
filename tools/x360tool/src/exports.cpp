// Ordinal → name tables for xboxkrnl.exe and xam.xex, from the tables shipped with XenonRecomp
// (originally from Xenia, BSD license).
#include "exports.h"

#include <unordered_map>

namespace x360tool {

namespace {

enum { kFunction = 0, kVariable = 1 };

struct Row {
  uint32_t ordinal;
  const char* name;
  int type;
};

#define XE_EXPORT(module, ordinal, name, type) {ordinal, #name, type}

constexpr Row kKernel[] = {
#include <xbox/xboxkrnl_table.inc>
};
constexpr Row kXam[] = {
#include <xbox/xam_table.inc>
};

#undef XE_EXPORT

template <size_t N>
std::unordered_map<uint32_t, const Row*> index(const Row (&rows)[N]) {
  std::unordered_map<uint32_t, const Row*> m;
  for (const Row& r : rows) m.emplace(r.ordinal, &r);
  return m;
}

}  // namespace

std::optional<ExportInfo> lookup_export(const std::string& library, uint32_t ordinal) {
  static const auto kernel = index(kKernel);
  static const auto xam = index(kXam);
  const std::unordered_map<uint32_t, const Row*>* table = nullptr;
  if (library == "xboxkrnl.exe") table = &kernel;
  else if (library == "xam.xex") table = &xam;
  if (!table) return std::nullopt;
  auto it = table->find(ordinal);
  if (it == table->end()) return std::nullopt;
  return ExportInfo{it->second->name, it->second->type == kVariable};
}

}  // namespace x360tool
