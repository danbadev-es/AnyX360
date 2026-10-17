#include "x360/hle.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_set>

namespace x360::hle {

namespace {
uint8_t* g_base = nullptr;
}

void (*g_note_import)(const char* name) = nullptr;

void set_guest_base(uint8_t* base) { g_base = base; }
uint8_t* guest_base() { return g_base; }

void unimplemented(const char* name) {
  static std::mutex mutex;
  static std::unordered_set<std::string> seen;
  std::lock_guard lock(mutex);
  if (seen.insert(name).second) {
    std::fprintf(stderr, "[hle] unimplemented: %s\n", name);
  }
}

void guest_function_called(uint32_t address, const char* name, const PPCContext& ctx) {
  static std::mutex mutex;
  static std::unordered_set<uint32_t> seen;
  std::lock_guard lock(mutex);
  if (seen.insert(address).second) {
    std::fprintf(stderr, "[hle] first call of %s (sub_%08X): r3 %08X r4 %08X r5 %08X r6 %08X, from %08X\n", name,
                 address, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, uint32_t(ctx.lr));
  }
}

}  // namespace x360::hle
