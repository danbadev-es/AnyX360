#include "xex_patch.h"

#include <xex_patcher.h>

namespace x360tool {

bool apply_xex_patch(std::span<const uint8_t> base, std::span<const uint8_t> patch, std::vector<uint8_t>& out,
                     std::string& error) {
  out.clear();
  if (base.size() < 0x18 || patch.size() < 0x18) {
    error = "file too small";
    return false;
  }
  switch (XexPatcher::apply(base.data(), base.size(), patch.data(), patch.size(), out, false)) {
    case XexPatcher::Result::Success:
      return true;
    case XexPatcher::Result::XexFileUnsupported:
      error = "the executable's format is not supported";
      break;
    case XexPatcher::Result::XexFileInvalid:
      error = "the executable is invalid";
      break;
    case XexPatcher::Result::PatchFileInvalid:
      error = "the patch is invalid";
      break;
    case XexPatcher::Result::PatchIncompatible:
      error = "the patch is for another executable";
      break;
    case XexPatcher::Result::PatchFailed:
      error = "the patch failed (damaged data)";
      break;
    default:
      error = "the patch is not supported";
      break;
  }
  out.clear();
  return false;
}

}  // namespace x360tool
