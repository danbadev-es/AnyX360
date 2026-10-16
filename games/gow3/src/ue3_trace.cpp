// UnrealScript call tracing for Gears of War 3 (debugging aid).
//
// ANYX360_UE3_TRACE="<seconds>[,<filter>]" logs, after <seconds>, the first call of every script
// function ("Class.Function" of the calling object) made through EX_VirtualFunction and
// EX_FinalFunction. With a filter, every call whose text contains it is logged instead.
//
// Addresses below belong to this build of the game (TU0 default.xex):
//   GNatives            0x833CDFD8 (EX_VirtualFunction = 0x1B, EX_FinalFunction = 0x1C)
//   FFrame              Object +0x14, Code +0x18
//   UObject             Outer +0x28, Name +0x2C, Class +0x34
// Names are resolved by walking the FNameEntry pools ([be32 index << 1][be32 hash next][ANSI text]).
#include <algorithm>
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

constexpr uint32_t kExecVirtualFunction = 0x822CD110;
constexpr uint32_t kExecFinalFunction = 0x822CD180;

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
  return (object >= 0x00010000 && object < 0x70000000) || (object >= 0x82000000 && object < 0x835B0000);
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

// NUL-terminated UTF-16BE text as ASCII.
std::string WideString(uint32_t address) {
  std::string out;
  for (uint32_t i = 0; address && i < 256; ++i) {
    uint16_t c = Load<uint16_t>(address + i * 2);
    if (!c) break;
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

extern "C" PPC_FUNC(__imp__sub_822B6CD0);
PPC_FUNC(sub_822B6CD0) {
  // FAsyncPackage::CreateLinker (polled every tick while the package is pending): name at +4.
  if (gow3::g_hooks_enabled) {
    static std::string last;
    std::string name = gow3::FString(ctx.r3.u32 + 4);
    if (name != last) KLOG_I("async package %s", name.c_str());
    last = name;
  }
  __imp__sub_822B6CD0(ctx, base);
}

// GearEngine::LoadMap(FURL& URL, ...): FURL Map at +28, options TArray<FString> at +40.
extern "C" PPC_FUNC(__imp__sub_82A0B7A0);
PPC_FUNC(sub_82A0B7A0) {
  if (gow3::g_hooks_enabled) {
    uint32_t url = ctx.r4.u32;
    std::string options;
    uint32_t data = Load<uint32_t>(url + 40);
    for (uint32_t i = 0, n = Load<uint32_t>(url + 44); i < n && i < 32; ++i)
      options += "?" + gow3::FString(data + i * 12);
    KLOG_I("call GearEngine::LoadMap(%s%s)", gow3::FString(url + 28).c_str(), options.c_str());
  }
  uint32_t error = ctx.r6.u32;
  __imp__sub_82A0B7A0(ctx, base);
  if (gow3::g_hooks_enabled) {
    uint32_t world = Load<uint32_t>(0x83429828);
    uint32_t world_info = world ? Load<uint32_t>(Load<uint32_t>(Load<uint32_t>(world + 80) + 60)) : 0;
    KLOG_I("     LoadMap -> %u, error '%s', WorldInfo+916 = %u", ctx.r3.u32, gow3::FString(error).c_str(),
           world_info ? Load<uint8_t>(world_info + 916) : 0xFF);
  }
}
GOW3_TRACE_CALL(82A10F38, "GearEngine::LoadCheckpoint")
GOW3_TRACE_CALL(82A11138, "GearEngine::ApplyCheckpoint?")
GOW3_TRACE_CALL(82A0B678, "GearEngine stream checkpoint levels")
GOW3_TRACE_CALL(82A0BF40, "GearEngine checkpoint state 3")
GOW3_TRACE_CALL(82A08250, "Checkpoint kismet events")
GOW3_TRACE_CALL(82A10E38, "GearEngine::SaveCheckpoint")

// _wcsicmp (case-insensitive UTF-16 compare): logged when comparing game class names.
extern "C" PPC_FUNC(__imp__sub_82CCAB98);
PPC_FUNC(sub_82CCAB98) {
  std::string a, b;
  if (gow3::g_hooks_enabled) {
    a = gow3::WideString(ctx.r3.u32);
    b = gow3::WideString(ctx.r4.u32);
  }
  uint64_t lr = ctx.lr;
  __imp__sub_82CCAB98(ctx, base);
  ctx.lr = lr;
  if (gow3::g_hooks_enabled && (a.find("GameSP") != std::string::npos || b.find("GameSP") != std::string::npos ||
                                 ctx.lr == 0x826DE7AC))
    KLOG_I("call wcsicmp(%s, %s) = %d", a.c_str(), b.c_str(), ctx.r3.s32);
}

GOW3_TRACE_CALL(82A40E50, "LoadMap chapter: get chapter list")
GOW3_TRACE_CALL(8253C3F8, "LoadMap chapter: 8253C3F8")
GOW3_TRACE_CALL(8253C728, "LoadMap chapter: 8253C728")
GOW3_TRACE_CALL(82A3FBC0, "LoadMap chapter: 82A3FBC0")
GOW3_TRACE_CALL(82651D80, "LoadMap chapter: 82651D80")
GOW3_TRACE_CALL(8253A428, "UGameEngine::LoadMap")

// IsTransitionMap-like check used by GearEngine::LoadMap to skip the chapter start.
extern "C" PPC_FUNC(__imp__sub_826E80B8);
PPC_FUNC(sub_826E80B8) {
  uint32_t world_info = ctx.r3.u32;
  uint32_t flags = gow3::g_hooks_enabled ? Load<uint32_t>(world_info + 696) : 0;
  uint64_t lr = ctx.lr;
  __imp__sub_826E80B8(ctx, base);
  if (gow3::g_hooks_enabled)
    KLOG_I("call IsTransitionMap(%s %08X, flags %08X) = %u lr %08llX", gow3::ClassName(world_info).c_str(),
           world_info, flags, ctx.r3.u32, (unsigned long long)lr);
}

GOW3_TRACE_CALL(826DE390, "826DE390 (sets WorldInfo transition flag)")

// Crash investigation: sub_82683148(this, index, a, b, object, ...) walks object+292 entries chosen
// through a byte table at this+828/840.
extern "C" PPC_FUNC(__imp__sub_82683148);
PPC_FUNC(sub_82683148) {
  if (gow3::g_hooks_enabled) {
    std::lock_guard lock(gow3::g_mutex);
    uint32_t self = ctx.r3.u32, object = ctx.r7.u32, index = ctx.r4.u32;
    uint32_t table = (ctx.r5.u32 || ctx.r6.u32) ? self + 828 : self + 840;
    uint32_t count = Load<uint32_t>(table + 4);
    uint32_t mapped = count > 0 ? Load<uint8_t>(Load<uint32_t>(table) + index) : 255;
    uint32_t entries = gow3::ValidObject(object) ? Load<uint32_t>(object + 292) : 0;
    uint32_t entry_count = gow3::ValidObject(object) ? Load<uint32_t>(object + 296) : 0;
    uint32_t node = (entries && mapped != 255 && mapped < entry_count) ? Load<uint32_t>(entries + mapped * 12 + 8) : 0;
    static int logged = 0;
    bool bad = mapped != 255 && (mapped >= entry_count || (node && !gow3::ValidObject(node)));
    if (bad || logged < 10) {
      ++logged;
      KLOG_I("82683148: self %s %08X index %u table[%u]=%u object %s.%s %08X entries %u node %08X%s",
             gow3::ClassName(self).c_str(), self, index, count, mapped, gow3::ClassName(object).c_str(),
             gow3::ObjectName(object).c_str(), object, entry_count, node, bad ? "  <-- BAD" : "");
      if (bad) {
        std::string bytes;
        char hex[4];
        uint32_t data = Load<uint32_t>(table);
        for (uint32_t i = 0; i < count; ++i) {
          std::snprintf(hex, sizeof(hex), "%02X ", Load<uint8_t>(data + i));
          bytes += hex;
        }
        KLOG_I("82683148: table data %08X (max %u): %s", data, Load<uint32_t>(table + 8), bytes.c_str());
      }
    }
  }
  __imp__sub_82683148(ctx, base);
}

// Experiment switch: ANYX360_OLD_MEMSET reproduces the truncated memset (tail bytes 2-3 not written).
extern "C" PPC_FUNC(__imp__sub_82CC9EA0);
PPC_FUNC(sub_82CC9EA0) {
  static const bool old = std::getenv("ANYX360_OLD_MEMSET") != nullptr;
  if (!old) {
    __imp__sub_82CC9EA0(ctx, base);
    return;
  }
  uint32_t dst = ctx.r3.u32, value = ctx.r4.u8, size = ctx.r5.u32;
  // Byte stores up to 4-byte alignment, words for the body, then only the first tail byte.
  uint32_t prefix = std::min<uint32_t>(size, (4 - (dst & 3)) & 3);
  uint32_t body = (size - prefix) & ~3u;
  uint32_t tail = size - prefix - body;
  for (uint32_t i = 0; i < prefix + body; ++i) Store<uint8_t>(dst + i, uint8_t(value));
  if (tail) Store<uint8_t>(dst + prefix + body, uint8_t(value));
}

#endif  // GOW3_DISABLE_HOOKS

#ifndef GOW3_DISABLE_HOOKS
// Crash investigation: StaticFindObjectFastInternal walks GObjHashOuter chains through
// UObject::HashOuterNext (+0x10). Before walking, validate the chain and report the object whose
// link is corrupt (with ANYX360_UE3_TRACE).
extern "C" PPC_FUNC(__imp__sub_8230F098);
PPC_FUNC(sub_8230F098) {
  if (gow3::g_hooks_enabled && ctx.r4.u32) {
    // (srawi outer,4) ^ FName.Index ^ FName.Number, 0x8000 buckets (rlwinm ...,2,15,29).
    uint32_t hash = (uint32_t(ctx.r4.s32 >> 4) ^ ctx.r5.u32 ^ uint32_t(ctx.r5.u64 >> 32)) & 0x7FFF;
    uint32_t object = Load<uint32_t>(0x833D63E8 + hash * 4);
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
  __imp__sub_8230F098(ctx, base);
}
#endif
