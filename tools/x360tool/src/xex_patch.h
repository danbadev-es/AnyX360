// Title updates: a default.xexp (an XEX2 delta patch, see Xex::delta_patch) applied to the
// executable it updates, with XenonRecomp's patcher (XenonUtils/xex_patcher.cpp, adapted from
// Xenia's). The result is the updated executable, unencrypted and uncompressed: what the recompiler
// reads and what the runtime loads. Its own library (x360_xex_patch), shared by x360tool and the
// runtime, so XenonUtils' headers stay out of the other code.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace x360tool {

// Applies `patch` to `base` into `out`. False, with `error` saying why, if the patch isn't one, isn't
// for this executable (its key or headers don't match) or fails.
bool apply_xex_patch(std::span<const uint8_t> base, std::span<const uint8_t> patch, std::vector<uint8_t>& out,
                     std::string& error);

}  // namespace x360tool
