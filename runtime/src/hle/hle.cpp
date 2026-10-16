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

}  // namespace x360::hle
