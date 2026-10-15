#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace x360tool {

struct ExportInfo {
  const char* name;
  bool is_variable;
};

// Looks up a system export by library name ("xboxkrnl.exe", "xam.xex") and ordinal.
std::optional<ExportInfo> lookup_export(const std::string& library, uint32_t ordinal);

}  // namespace x360tool
