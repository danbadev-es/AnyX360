// Title loader: maps the decrypted XEX image, fills the recompiled function table, resolves
// variable imports and prepares the structures XAPI expects (module handle, XEX header copy).
#include <array>
#include <optional>
#include <unordered_map>

#include "exports.h"
#include "x360/gpu.h"
#include <atomic>
#include <cstdlib>
#include <utility>
#include "x360/kernel.h"
#include "xex.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::kernel {

namespace {

LoadedTitle g_title;
TitleInfo g_paths;
std::unordered_map<std::string, uint32_t> g_variables;

// Every code slot without a recompiled function points here, so a bad indirect call reports the
// target instead of jumping to null.
PPC_FUNC(UnknownIndirectCall) {
  (void)base;
  Fatal("indirect call to an address without recompiled code: %08X (lr %08llX)", ctx.ctr.u32, ctx.lr);
}

void FillFunctionTable() {
  uint64_t slots = PPC_CODE_SIZE / 4;
  auto** table = reinterpret_cast<PPCFunc**>(g_base + PPC_IMAGE_BASE + PPC_IMAGE_SIZE);
  for (uint64_t i = 0; i < slots; ++i) table[i] = UnknownIndirectCall;
  size_t count = 0;
  for (const PPCFuncMapping* m = PPCFuncMappings; m->guest != 0; ++m) {
    if (m->host) {
      PPC_LOOKUP_FUNC(g_base, m->guest) = m->host;
      ++count;
    }
  }
  KLOG_I("function table: %zu recompiled functions", count);
}

// Debugging aid: ANYX360_HOOK="addr,addr,..." logs calls made through the function table (indirect
// calls, e.g. UnrealScript natives) to those guest functions, then runs the original.
struct Hook {
  uint32_t guest = 0;
  PPCFunc* original = nullptr;
  std::atomic<uint32_t> calls{0};
};
Hook g_hooks[8];

template <int N>
PPC_FUNC(HookTrampoline) {
  Hook& h = g_hooks[N];
  uint32_t n = ++h.calls;
  if (n <= 20 || (n & (n - 1)) == 0)
    KLOG_I("hook %08X call #%u: r3 %08X r4 %08X r5 %08X lr %08llX", h.guest, n, ctx.r3.u32, ctx.r4.u32,
           ctx.r5.u32, (unsigned long long)ctx.lr);
  h.original(ctx, base);
}

template <int... N>
void InstallHooks(std::integer_sequence<int, N...>) {
  const char* env = std::getenv("ANYX360_HOOK");
  if (!env) return;
  PPCFunc* trampolines[] = {HookTrampoline<N>...};
  int slot = 0;
  for (const char* p = env; *p && slot < int(sizeof...(N));) {
    uint32_t guest = uint32_t(std::strtoul(p, nullptr, 16));
    if (guest >= PPC_CODE_BASE && guest < PPC_CODE_BASE + PPC_CODE_SIZE) {
      g_hooks[slot].guest = guest;
      g_hooks[slot].original = PPC_LOOKUP_FUNC(g_base, guest);
      PPC_LOOKUP_FUNC(g_base, guest) = trampolines[slot];
      KLOG_I("hook installed at %08X", guest);
      ++slot;
    }
    while (*p && *p != ',') ++p;
    if (*p == ',') ++p;
  }
}

uint32_t AllocVariable(const char* name, uint32_t size) {
  uint32_t addr = SystemAlloc(size);
  g_variables[name] = addr;
  return addr;
}

void SetupKernelVariables() {
  // XboxHardwareInfo: flags (0x20 = HDD present), CPU count.
  uint32_t hw = AllocVariable("XboxHardwareInfo", 16);
  Store<uint32_t>(hw + 0, 0x20);
  Store<uint8_t>(hw + 4, 6);

  // XboxKrnlVersion: 2.0.17559.0
  uint32_t version = AllocVariable("XboxKrnlVersion", 8);
  Store<uint16_t>(version + 0, 2);
  Store<uint16_t>(version + 2, 0);
  Store<uint16_t>(version + 4, 17559);
  Store<uint16_t>(version + 6, 0);

  // KeTimeStampBundle is kept up to date by the time thread (InitTime).
  AllocVariable("KeTimeStampBundle", 0x18);

  uint32_t clock = AllocVariable("VdGpuClockInMHz", 4);
  Store<uint32_t>(clock, 500);

  uint32_t hsio = AllocVariable("VdHSIOCalibrationLock", 0x20);
  InitializeDispatcher(hsio, kEventSynchronization, 1);

  AllocVariable("VdGlobalDevice", 4);
  AllocVariable("VdGlobalXamDevice", 4);

  // Debug/cert monitors: null pointers mean "no monitor attached" (a non-null pointer makes the
  // title call the monitor's callbacks).
  AllocVariable("KeDebugMonitorData", 4);
  AllocVariable("KeCertMonitorData", 4);

  // The quoted name of the running XEX.
  uint32_t cmd = AllocVariable("ExLoadedCommandLine", 0x400);
  std::string command_line = "\"" + g_paths.xex_path.filename().string() + "\"";
  std::strncpy(Ptr<char>(cmd), command_line.c_str(), 0x3FF);

  // Object type descriptors (only their addresses matter to the title).
  for (const char* name : {"ExThreadObjectType", "ExEventObjectType", "ExMutantObjectType", "ExSemaphoreObjectType",
                           "ExTimerObjectType", "IoFileObjectType", "IoDeviceObjectType", "IoCompletionObjectType",
                           "ObDirectoryObjectType", "ObSymbolicLinkObjectType"}) {
    AllocVariable(name, 0x40);
  }

  // XexExecutableModuleHandle: address of a pointer to the module's loader entry.
  uint32_t module_handle = AllocVariable("XexExecutableModuleHandle", 4);
  Store<uint32_t>(module_handle, g_title.ldr_entry);
}

void ResolveImports(const x360tool::Xex& xex) {
  const uint32_t base = xex.base_address();
  for (const auto& lib : xex.import_libraries()) {
    for (uint32_t record_address : lib.record_addresses) {
      uint32_t record = Load<uint32_t>(record_address);
      uint8_t type = uint8_t(record >> 24);
      uint16_t ordinal = uint16_t(record & 0xFFFF);
      auto info = x360tool::lookup_export(lib.name, ordinal);
      if (type != 0) continue;  // thunks: the recompiler turned them into calls to __imp__<Name>
      if (!info || !info->is_variable) {
        // Function "IAT" slot: point it at the thunk code so indirect use still works.
        continue;
      }
      uint32_t value = KernelVariable(info->name);
      if (!value) {
        KLOG_W("import variable %s!%s has no value", lib.name.c_str(), info->name);
        continue;
      }
      Store<uint32_t>(record_address, value);
    }
  }
  (void)base;
}

std::optional<std::array<uint8_t, 16>> LoadKey(const std::filesystem::path& path) {
  std::error_code ec;
  if (path.empty() || !std::filesystem::exists(path, ec)) return std::nullopt;
  auto raw = x360tool::read_file(path);
  std::vector<uint8_t> key = raw;
  if (raw.size() != 16) {
    auto parsed = x360tool::parse_hex(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    if (!parsed) return std::nullopt;
    key = *parsed;
  }
  if (key.size() != 16) return std::nullopt;
  std::array<uint8_t, 16> out;
  std::copy(key.begin(), key.end(), out.begin());
  return out;
}

}  // namespace

const LoadedTitle& Title() { return g_title; }
const TitleInfo& TitlePaths() { return g_paths; }

uint32_t KernelVariable(const char* name) {
  auto it = g_variables.find(name);
  return it == g_variables.end() ? 0 : it->second;
}

// Indirect calls go through the function table, which only covers the title's code. The address
// given out is a zero word of padding between two of its functions (an invalid instruction, so no
// code starts or runs there), taken from the end of the code.
uint32_t GuestAddressOf(PPCFunc* host) {
  static std::mutex mutex;
  static std::unordered_map<PPCFunc*, uint32_t> given;
  static uint32_t next = uint32_t(PPC_CODE_BASE + PPC_CODE_SIZE);
  std::lock_guard lock(mutex);
  if (auto it = given.find(host); it != given.end()) return it->second;
  while (next > PPC_CODE_BASE) {
    next -= 4;
    if (Load<uint32_t>(next) != 0 || PPC_LOOKUP_FUNC(g_base, next) != UnknownIndirectCall) continue;
    PPC_LOOKUP_FUNC(g_base, next) = host;
    given[host] = next;
    return next;
  }
  // No free padding word: the analyser made a function of every one (Resident Evil 6's padding
  // words between functions are all function starts). A zero word is no instruction a title can
  // call, so its table slot can be taken all the same.
  static uint32_t next_taken = uint32_t(PPC_CODE_BASE + PPC_CODE_SIZE);
  while (next_taken > PPC_CODE_BASE) {
    next_taken -= 4;
    if (Load<uint32_t>(next_taken) != 0) continue;
    bool used = false;
    for (const auto& [function, address] : given) used = used || address == next_taken;
    if (used) continue;
    PPC_LOOKUP_FUNC(g_base, next_taken) = host;
    given[host] = next_taken;
    return next_taken;
  }
  return 0;
}

// Windows' default timer resolution (15.6 ms) makes every short sleep and timed wait (the title's
// KeDelayExecutionThread, vblank and audio pacing) last ~15.6 ms. Use the finest one, as Xenia does.
void RaiseTimerResolution() {
  using QueryFn = LONG(NTAPI*)(PULONG, PULONG, PULONG);
  using SetFn = LONG(NTAPI*)(ULONG, BOOLEAN, PULONG);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto query = reinterpret_cast<QueryFn>(GetProcAddress(ntdll, "NtQueryTimerResolution"));
  auto set = reinterpret_cast<SetFn>(GetProcAddress(ntdll, "NtSetTimerResolution"));
  if (!query || !set) return;
  ULONG minimum, maximum, current;  // in 100 ns units; "maximum" is the finest
  query(&minimum, &maximum, &current);
  set(maximum, TRUE, &current);
  KLOG_I("timer resolution: %.2f ms", current / 10000.0);
}

// Windows 11 may run background-looking processes on efficiency cores at lower clocks (EcoQoS) and
// ignore their timer resolution while the window is covered: opt out of both.
void DisablePowerThrottling() {
  PROCESS_POWER_THROTTLING_STATE state{};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
  state.StateMask = 0;
  if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state)))
    KLOG_W("power throttling opt-out failed (%lu)", GetLastError());
}

bool LoadTitle(const TitleInfo& info) {
  g_paths = info;

  std::unique_ptr<x360tool::Xex> xex;
  std::vector<uint8_t> image;
  // The game folder's executable, or the one its title update makes (PrepareTitleUpdate).
  const std::filesystem::path& path = info.image_path.empty() ? info.xex_path : info.image_path;
  try {
    xex = std::make_unique<x360tool::Xex>(x360tool::read_file(path));
    std::string used;
    image = xex->extract_image(LoadKey(info.key_path), &used);
    const auto execution = xex->execution_info();
    KLOG_I("loaded %s (%zu bytes, key: %s, version %s)", path.string().c_str(), image.size(), used.c_str(),
           execution ? execution->version.str().c_str() : "?");
  } catch (const std::exception& e) {
    KLOG_E("cannot load %s: %s", path.string().c_str(), e.what());
    return false;
  }

  g_title.image_base = xex->base_address();
  g_title.image_size = uint32_t(image.size());
  g_title.entry_point = xex->entry_point().value_or(0);
  if (g_title.image_base != PPC_IMAGE_BASE || g_title.image_size > PPC_IMAGE_SIZE) {
    KLOG_E("image %08X+%X does not match the recompiled image %08llX+%llX (different executable?)",
           g_title.image_base, g_title.image_size, PPC_IMAGE_BASE, PPC_IMAGE_SIZE);
    return false;
  }
  if (!ReserveImage(g_title.image_base, g_title.image_size)) {
    KLOG_E("could not reserve the image at %08X", g_title.image_base);
    return false;
  }
  std::memcpy(Ptr(g_title.image_base), image.data(), image.size());
  // Recompiled function table: one host pointer per guest instruction, right after the image.
  if (!ReserveImage(uint32_t(PPC_IMAGE_BASE + PPC_IMAGE_SIZE), uint32_t(PPC_CODE_SIZE * 2))) {
    KLOG_E("could not reserve the function table");
    return false;
  }

  if (auto v = xex->opt_value(x360tool::xex_key::kDefaultStackSize)) g_title.stack_size = *v;
  if (auto e = xex->execution_info()) g_title.title_id = e->title_id;
  if (auto off = xex->opt_offset(x360tool::xex_key::kTlsInfo)) {
    const uint8_t* p = xex->data().data() + *off;
    g_title.tls_slot_count = load_be<uint32_t>(p + 0);
    g_title.tls_raw_address = load_be<uint32_t>(p + 4);
    g_title.tls_data_size = load_be<uint32_t>(p + 8);
    g_title.tls_raw_size = load_be<uint32_t>(p + 12);
    if (!g_title.tls_slot_count) g_title.tls_slot_count = 64;
  }

  // Copy of the XEX header for RtlImageXexHeaderField, and the loader entry pointing at it.
  uint32_t header_size = xex->pe_offset();
  g_title.xex_header = SystemAlloc(header_size);
  std::memcpy(Ptr(g_title.xex_header), xex->data().data(), header_size);
  g_title.ldr_entry = SystemAlloc(0x80);
  uint32_t ldr = g_title.ldr_entry;
  Store<uint32_t>(ldr + 0x00, ldr);  // list links point to themselves
  Store<uint32_t>(ldr + 0x04, ldr);
  Store<uint32_t>(ldr + 0x18, g_title.image_base);
  Store<uint32_t>(ldr + 0x1C, g_title.image_base);
  Store<uint32_t>(ldr + 0x20, g_title.image_size);
  Store<uint32_t>(ldr + 0x3C, g_title.entry_point);
  Store<uint32_t>(ldr + 0x58, g_title.xex_header);

  FillFunctionTable();
  InstallHooks(std::make_integer_sequence<int, 8>());
  if (info.on_function_table) info.on_function_table();
  SetupKernelVariables();
  ResolveImports(*xex);

  KLOG_I("title %08X: entry %08X, stack %X, TLS %u slots + %X bytes", g_title.title_id, g_title.entry_point,
         g_title.stack_size, g_title.tls_slot_count, g_title.tls_data_size);

  InitTime();
  InitIo(info);
  InitXam();
  InitVideo();
  InitAudio();
  return true;
}

namespace {

// The title's name from its SPA resource (an XDBF database named after the title ID): string 0x8000
// of the default language's string table (XSTC), else English, else any language.
std::string ReadTitleName() {
  char resource_name[9];
  std::snprintf(resource_name, sizeof(resource_name), "%08X", g_title.title_id);
  uint32_t base, size;
  if (!FindXexResource(resource_name, base, size) || size < 24 || Load<uint32_t>(base) != 0x58444246)  // 'XDBF'
    return {};
  uint32_t entry_count = Load<uint32_t>(base + 8), free_count = Load<uint32_t>(base + 16);
  uint64_t data = 24 + 18ull * entry_count + 8ull * free_count;
  if (data > size) return {};
  // Entries: {be16 section, be64 id, be32 offset (from the data), be32 size}.
  auto find = [&](uint16_t section, uint64_t id, uint32_t& at, uint32_t& length) {
    for (uint32_t i = 0; i < entry_count; ++i) {
      uint32_t entry = base + 24 + i * 18;
      if (Load<uint16_t>(entry) != section || Load<uint64_t>(entry + 2) != id) continue;
      uint64_t offset = data + Load<uint32_t>(entry + 10);
      length = Load<uint32_t>(entry + 14);
      if (offset + length > size) return false;
      at = base + uint32_t(offset);
      return true;
    }
    return false;
  };
  // String tables (section 3, id = language): 'XSTR', version, size, be16 count, then
  // {be16 id, be16 length, UTF-8}.
  auto title_string = [&](uint64_t language) -> std::string {
    uint32_t at, length;
    if (!find(3, language, at, length) || length < 14 || Load<uint32_t>(at) != 0x58535452) return {};
    uint32_t p = at + 14, end = at + length;
    for (uint16_t i = 0, count = Load<uint16_t>(at + 12); i < count && p + 4 <= end; ++i) {
      uint16_t id = Load<uint16_t>(p), text_length = Load<uint16_t>(p + 2);
      if (p + 4 + text_length > end) break;
      if (id == 0x8000) return std::string(Ptr<const char>(p + 4), text_length);
      p += 4 + text_length;
    }
    return {};
  };
  uint32_t language = 1, at, length;
  if (find(1, 0x58535443, at, length) && length >= 16) language = Load<uint32_t>(at + 12);  // 'XSTC'
  std::string name = title_string(language);
  if (name.empty()) name = title_string(1);
  for (uint64_t l = 2; name.empty() && l < 32; ++l) name = title_string(l);
  if (!name.empty()) KLOG_I("title: %s (XDBF %s, language %u)", name.c_str(), resource_name, language);
  return name;
}

}  // namespace

bool FindXexResource(const std::string& name, uint32_t& address, uint32_t& size) {
  // RESOURCE_INFO (0x2FF) header: {be32 size; {char name[8]; be32 address; be32 size}[]}.
  uint32_t header = g_title.xex_header;
  if (!header) return false;
  uint32_t count = Load<uint32_t>(header + 0x14);
  for (uint32_t i = 0; i < count; ++i) {
    if (Load<uint32_t>(header + 0x18 + i * 8) != 0x000002FF) continue;
    uint32_t info = header + Load<uint32_t>(header + 0x1C + i * 8);
    uint32_t entries = (Load<uint32_t>(info) - 4) / 16;
    for (uint32_t e = 0; e < entries; ++e) {
      uint32_t entry = info + 4 + e * 16;
      if (name != std::string(Ptr<const char>(entry), strnlen(Ptr<const char>(entry), 8))) continue;
      address = Load<uint32_t>(entry + 8);
      size = Load<uint32_t>(entry + 12);
      return true;
    }
  }
  return false;
}

int Run(const TitleInfo& info) {
  InstallCrashHandler();
  if (!InitMemory()) return 1;
  SetupWriteWatch();
  // The window, Direct3D 12 and the GPU must exist before the title runs; this thread becomes the
  // UI thread.
  RaiseTimerResolution();
  DisablePowerThrottling();
  std::string window_title = info.title_name.empty() ? "AnyX360" : "AnyX360 - " + info.title_name;
  if (!gpu::Initialize(window_title.c_str(), info.display)) return 1;
  if (!LoadTitle(info)) return 1;
  std::string name = ReadTitleName();
  if (name.empty()) name = info.title_name;
  if (name.empty()) {
    char fallback[24];
    std::snprintf(fallback, sizeof(fallback), "Title %08X", g_title.title_id);
    name = fallback;
  }
  gpu::SetWindowTitle("AnyX360 - " + name);
  gpu::InitializeShaderStorage(info.user_root / "cache", g_title.title_id);

  std::thread([processor = info.main_thread_processor] {
    uint32_t code = RunMainThread(g_title.entry_point, g_title.stack_size, processor);
    KLOG_I("title exited with %08X", code);
    gpu::RequestQuit(int(code));
  }).detach();
  return gpu::RunMessageLoop();
}

}  // namespace x360::kernel
