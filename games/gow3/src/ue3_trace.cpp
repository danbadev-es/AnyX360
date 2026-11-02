// UnrealScript call tracing for Gears of War 3 (debugging aid).
//
// ANYX360_UE3_TRACE="<seconds>[,<filter>]" logs, after <seconds>, the first call of every script
// function ("Class.Function" of the calling object) made through EX_VirtualFunction and
// EX_FinalFunction. With a filter, every call whose text contains it is logged instead.
//
// Addresses below belong to the build it is recompiled from (the title update's 0.0.6.7), remapped
// from the disc's 0.0.0.7 by matching the functions' code:
//   GNatives holds EX_VirtualFunction (0x1B) and EX_FinalFunction (0x1C)
//   FFrame              Object +0x14, Code +0x18
//   UObject             Outer +0x28, Name +0x2C, Class +0x34
// Names are resolved by walking the FNameEntry pools ([be32 index << 1][be32 hash next][ANSI text]).
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace x360::kernel;

namespace gow3 {
namespace {

constexpr uint32_t kExecVirtualFunction = 0x822DD460;
constexpr uint32_t kExecFinalFunction = 0x822DD4D0;

PPCFunc* g_virtual_original = nullptr;
PPCFunc* g_final_original = nullptr;
double g_start_after = 0;
std::string g_filter;

std::mutex g_mutex;
std::unordered_map<uint32_t, std::string> g_names;
bool g_names_built = false;
std::unordered_set<std::string> g_seen;

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

bool ValidHashNext(uint32_t v) { return v == 0 || (v >= 0x40000000 && v < 0x80000000); }

// Length of the printable ANSI string at p (0 if not a plausible name).
size_t NameLength(const uint8_t* p, const uint8_t* end) {
  size_t n = 0;
  while (p + n < end && n < 256) {
    uint8_t c = p[n];
    if (c == 0) return n;
    if (c < 0x20 || c > 0x7E) return 0;
    ++n;
  }
  return 0;
}

void BuildNames() {
  g_names[0] = "None";
  uint8_t* host = g_base + 0x40000000ull;
  uint8_t* host_end = g_base + 0x80000000ull;
  while (host < host_end) {
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(host, &info, sizeof(info))) break;
    uint8_t* end = static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize;
    bool readable = info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                    (info.Protect & (PAGE_READONLY | PAGE_READWRITE));
    if (readable) {
      for (uint8_t* p = host; p + 12 < end;) {
        uint32_t header = be32(p);
        uint32_t index = header >> 1;
        size_t length;
        if ((header & 1) || index == 0 || index > 0x200000 || !ValidHashNext(be32(p + 4)) ||
            !(length = NameLength(p + 8, end))) {
          ++p;
          continue;
        }
        // Require the next entry of the pool to follow with the next index.
        uint8_t* next = p + 8 + length + 1;
        if (next + 12 < end && (be32(next) >> 1) == index + 1 && ValidHashNext(be32(next + 4)) &&
            NameLength(next + 8, end)) {
          g_names.emplace(index, std::string(reinterpret_cast<char*>(p + 8), length));
          p = next;
        } else {
          ++p;
        }
      }
    }
    host = end;
  }
  KLOG_I("ue3 trace: %zu names", g_names.size());
}

double Seconds();

std::string Name(uint32_t index) {
  auto it = g_names.find(index);
  if (it == g_names.end() && index < 0x200000) {
    // Names created after the last scan (new packages): rescan, at most every 2 seconds.
    static double last_scan = -10;
    if (Seconds() - last_scan > 2) {
      last_scan = Seconds();
      BuildNames();
      it = g_names.find(index);
    }
  }
  return it != g_names.end() ? it->second : "#" + std::to_string(index);
}

// Objects live in the 4 KB (0x00010000+) and 64 KB (0x40000000+) virtual heaps, or in the image
// (intrinsic classes), and are aligned.
bool ValidObject(uint32_t object) {
  if (object & 3) return false;
  return (object >= 0x00010000 && object < 0x70000000) || (object >= 0x82000000 && object < 0x83620000);
}

std::string ObjectName(uint32_t object) {
  if (!ValidObject(object)) return "?";
  return Name(Load<uint32_t>(object + 0x2C));
}

std::string ClassName(uint32_t object) {
  if (!ValidObject(object)) return "?";
  return ObjectName(Load<uint32_t>(object + 0x34));
}

double Seconds() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void Note(PPCContext& ctx, bool final) {
  double now = Seconds();
  if (now < g_start_after) return;
  if (!ValidObject(ctx.r4.u32) && (ctx.r4.u32 < 0x70000000 || ctx.r4.u32 >= 0x7F000000)) return;
  uint32_t code = Load<uint32_t>(ctx.r4.u32 + 0x18);
  if (!ValidObject(code)) return;
  std::lock_guard lock(g_mutex);
  if (!g_names_built) {
    g_names_built = true;
    BuildNames();
  }
  std::string function;
  if (final) {
    uint32_t fn = Load<uint32_t>(code);
    if (!ValidObject(fn)) return;
    function = ObjectName(Load<uint32_t>(fn + 0x28)) + "." + ObjectName(fn);
  } else {
    function = Name(Load<uint32_t>(code));
  }
  std::string key = ClassName(ctx.r3.u32) + (final ? " -> " : " => ") + function;
  if (!g_filter.empty()) {
    if (key.find(g_filter) != std::string::npos)
      KLOG_I("ue3 %.2f %s (%s)", now, key.c_str(), ObjectName(ctx.r3.u32).c_str());
  } else if (g_seen.insert(key).second) {
    KLOG_I("ue3 %.2f %s", now, key.c_str());
  }
}

PPC_FUNC(TraceVirtualFunction) {
  Note(ctx, false);
  g_virtual_original(ctx, base);
}

PPC_FUNC(TraceFinalFunction) {
  Note(ctx, true);
  g_final_original(ctx, base);
}

// UTF-16BE FString {Data, Num, Max} as ASCII.
std::string FString(uint32_t address) {
  if (!ValidObject(address) && (address < 0x70000000 || address >= 0x7F000000)) return "?";
  uint32_t data = Load<uint32_t>(address);
  uint32_t count = Load<uint32_t>(address + 4);
  std::string out;
  for (uint32_t i = 0; data && i + 1 < count && i < 256; ++i) {
    uint16_t c = Load<uint16_t>(data + i * 2);
    out += (c >= 0x20 && c < 0x7F) ? char(c) : '?';
  }
  return out;
}

bool g_hooks_enabled = false;

}  // namespace

void InstallUe3Trace() {
  const char* env = std::getenv("ANYX360_UE3_TRACE");
  if (!env) return;
  g_hooks_enabled = true;
  g_start_after = std::atof(env);
  if (const char* comma = std::strchr(env, ',')) g_filter = comma + 1;
  Seconds();
  g_virtual_original = PPC_LOOKUP_FUNC(g_base, kExecVirtualFunction);
  g_final_original = PPC_LOOKUP_FUNC(g_base, kExecFinalFunction);
  PPC_LOOKUP_FUNC(g_base, kExecVirtualFunction) = TraceVirtualFunction;
  PPC_LOOKUP_FUNC(g_base, kExecFinalFunction) = TraceFinalFunction;
  KLOG_I("ue3 trace: script calls after %.0f s%s%s", g_start_after, g_filter.empty() ? "" : " matching ",
         g_filter.c_str());
}

}  // namespace gow3

#ifndef GOW3_DISABLE_HOOKS
// Direct-call hooks: XenonRecomp emits every function as a weak alias of __imp__sub_X, so a strong
// definition here takes over all call sites. They only log while ANYX360_UE3_TRACE is set.
#define GOW3_TRACE_CALL(address, label)                                                  \
  extern "C" PPC_FUNC(__imp__sub_##address);                                            \
  PPC_FUNC(sub_##address) {                                                             \
    if (gow3::g_hooks_enabled)                                                          \
      KLOG_I("call %s (r3 %08X r4 %08X r5 %08X lr %08llX)", label, ctx.r3.u32, ctx.r4.u32, \
             ctx.r5.u32, (unsigned long long)ctx.lr);                                   \
    __imp__sub_##address(ctx, base);                                                    \
  }

extern "C" PPC_FUNC(__imp__sub_822C7218);
PPC_FUNC(sub_822C7218) {
  // FAsyncPackage::CreateLinker (polled every tick while the package is pending): name at +4.
  if (gow3::g_hooks_enabled) {
    static std::string last;
    std::string name = gow3::FString(ctx.r3.u32 + 4);
    if (name != last) KLOG_I("async package %s", name.c_str());
    last = name;
  }
  __imp__sub_822C7218(ctx, base);
}

// GearEngine::LoadMap(FURL& URL, ...): FURL Map at +28, options TArray<FString> at +40.
extern "C" PPC_FUNC(__imp__sub_82A1D420);
PPC_FUNC(sub_82A1D420) {
  if (gow3::g_hooks_enabled) {
    uint32_t url = ctx.r4.u32;
    std::string options;
    uint32_t data = Load<uint32_t>(url + 40);
    for (uint32_t i = 0, n = Load<uint32_t>(url + 44); i < n && i < 32; ++i)
      options += "?" + gow3::FString(data + i * 12);
    KLOG_I("call GearEngine::LoadMap(%s%s)", gow3::FString(url + 28).c_str(), options.c_str());
  }
  uint32_t error = ctx.r6.u32;
  __imp__sub_82A1D420(ctx, base);
  if (gow3::g_hooks_enabled) {
    uint32_t world = Load<uint32_t>(0x83439FE8);
    uint32_t world_info = world ? Load<uint32_t>(Load<uint32_t>(Load<uint32_t>(world + 80) + 60)) : 0;
    KLOG_I("     LoadMap -> %u, error '%s', WorldInfo+916 = %u", ctx.r3.u32, gow3::FString(error).c_str(),
           world_info ? Load<uint8_t>(world_info + 916) : 0xFF);
  }
}
GOW3_TRACE_CALL(82A22B40, "GearEngine::LoadCheckpoint")
GOW3_TRACE_CALL(82A22D40, "GearEngine::ApplyCheckpoint?")
GOW3_TRACE_CALL(82A1D2F8, "GearEngine stream checkpoint levels")
GOW3_TRACE_CALL(82A1DBC0, "GearEngine checkpoint state 3")
GOW3_TRACE_CALL(82A19E80, "Checkpoint kismet events")
GOW3_TRACE_CALL(82A22A40, "GearEngine::SaveCheckpoint")

#endif  // GOW3_DISABLE_HOOKS

#ifndef GOW3_DISABLE_HOOKS
// Crash investigation: StaticFindObjectFastInternal walks GObjHashOuter chains through
// UObject::HashOuterNext (+0x10). Before walking, validate the chain and report the object whose
// link is corrupt (with ANYX360_UE3_TRACE).
extern "C" PPC_FUNC(__imp__sub_8231F8B8);
PPC_FUNC(sub_8231F8B8) {
  if (gow3::g_hooks_enabled && ctx.r4.u32) {
    // (srawi outer,4) ^ FName.Index ^ FName.Number, 0x8000 buckets (rlwinm ...,2,15,29).
    uint32_t hash = (uint32_t(ctx.r4.s32 >> 4) ^ ctx.r5.u32 ^ uint32_t(ctx.r5.u64 >> 32)) & 0x7FFF;
    uint32_t object = Load<uint32_t>(0x833E6BA8 + hash * 4);
    uint32_t previous = 0;
    for (int n = 0; object && n < 100000; ++n) {
      if (!gow3::ValidObject(object)) {
        std::string bytes;
        char hex[12];
        for (uint32_t i = 0; previous && i < 0x40; i += 4) {
          std::snprintf(hex, sizeof(hex), "%08X ", Load<uint32_t>(previous + i));
          bytes += hex;
        }
        std::lock_guard lock(gow3::g_mutex);
        KLOG_E("GObjHashOuter[%04X]: object %08X (%s %s) has bad HashOuterNext %08X; header: %s", hash,
               previous, gow3::ClassName(previous).c_str(), gow3::ObjectName(previous).c_str(), object,
               bytes.c_str());
        break;
      }
      previous = object;
      object = Load<uint32_t>(object + 16);
    }
  }
  __imp__sub_8231F8B8(ctx, base);
}
#endif
