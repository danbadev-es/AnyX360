// Title loader: maps the decrypted XEX image, fills the recompiled function table, resolves
// variable imports and prepares the structures XAPI expects (module handle, XEX header copy).
#include <array>
#include <optional>
#include <unordered_map>

#include "exports.h"
#include "x360/gpu.h"
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

  uint32_t cmd = AllocVariable("ExLoadedCommandLine", 0x400);
  std::strcpy(Ptr<char>(cmd), "\"default.xex\"");

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

bool LoadTitle(const TitleInfo& info) {
  g_paths = info;

  std::unique_ptr<x360tool::Xex> xex;
  std::vector<uint8_t> image;
  try {
    xex = std::make_unique<x360tool::Xex>(x360tool::read_file(info.xex_path));
    std::string used;
    image = xex->extract_image(LoadKey(info.key_path), &used);
    KLOG_I("loaded %s (%zu bytes, key: %s)", info.xex_path.string().c_str(), image.size(), used.c_str());
  } catch (const std::exception& e) {
    KLOG_E("cannot load %s: %s", info.xex_path.string().c_str(), e.what());
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

int Run(const TitleInfo& info) {
  InstallCrashHandler();
  if (!InitMemory()) return 1;
  // The window, Direct3D 12 and the GPU must exist before the title runs; this thread becomes the
  // UI thread.
  if (!gpu::Initialize("AnyX360")) return 1;
  if (!LoadTitle(info)) return 1;

  std::thread([] {
    uint32_t code = RunMainThread(g_title.entry_point, g_title.stack_size);
    KLOG_I("title exited with %08X", code);
    gpu::RequestQuit(int(code));
  }).detach();
  return gpu::RunMessageLoop();
}

}  // namespace x360::kernel
